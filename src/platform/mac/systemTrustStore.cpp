#include "../systemTrustStore.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "../../util/base64.hpp"
#include "../../util/writeLog.hpp"

/*
macOS's libcurl uses LibreSSL by default (SecureTransport is only a
compile-time alternative), which trusts /etc/ssl/cert.pem, not the
keychains: corporate roots installed by IT, an MDM profile or the user
("Always Trust") would be ignored. So the driver gives curl a PEM bundle of
the roots macOS trusts for TLS (CURLOPT_CAINFO_BLOB).

The decision follows the user's and the administrator's trust settings,
user domain first, the way the Security framework applies them:
  - an empty settings array means "trust as root for everything";
  - otherwise the first entry that applies to SSL (no policy, or the SSL
    policy, and no host name restriction) decides: TrustRoot/TrustAsRoot
    trusts, Unspecified defers to the next domain;
  - a Deny for any purpose excludes the certificate (a CA bundle can't
    express "not for TLS only");
  - with no decision, a system root counts when the Security framework's
    own evaluation (basic X.509 policy, no network) accepts it: Apple
    distrusts some roots it still lists as anchors.
(The SSL policy can't be used for that evaluation: it also checks the
certificate as a server certificate, e.g. at most 398 days of validity,
which roots never pass.)
Certificates trusted only for other purposes (EAP, S/MIME, ...) or with a
host name restriction are left out, as are expired ones.
*/

namespace platform {
namespace {

template <typename Ref> class CFHolder {
  public:
    explicit CFHolder(Ref ref = nullptr) : ref_(ref) {}
    ~CFHolder() {
      if (ref_) {
        CFRelease(ref_);
      }
    }
    CFHolder(const CFHolder&)            = delete;
    CFHolder& operator=(const CFHolder&) = delete;

    Ref get() const {
      return ref_;
    }
    Ref* out() {
      return &ref_;
    }

  private:
    Ref ref_;
};

enum class Decision {
  None, // No setting in this domain applies: ask the next one.
  Trust,
  Deny,
};

bool isSslPolicy(SecPolicyRef policy) {
  const CFHolder<CFDictionaryRef> properties(SecPolicyCopyProperties(policy));
  if (properties.get() == nullptr) {
    return false;
  }
  const auto oid = static_cast<CFStringRef>(
      CFDictionaryGetValue(properties.get(), kSecPolicyOid));
  return oid != nullptr && CFEqual(oid, kSecPolicyAppleSSL) != 0;
}

SInt32 settingResult(CFDictionaryRef entry) {
  // The result defaults to TrustRoot when it's absent.
  SInt32 result     = kSecTrustSettingsResultTrustRoot;
  const auto number = static_cast<CFNumberRef>(
      CFDictionaryGetValue(entry, kSecTrustSettingsResult));
  if (number != nullptr) {
    CFNumberGetValue(number, kCFNumberSInt32Type, &result);
  }
  return result;
}

Decision decisionInDomain(SecCertificateRef certificate,
                          SecTrustSettingsDomain domain) {
  CFHolder<CFArrayRef> settings;
  if (SecTrustSettingsCopyTrustSettings(certificate, domain, settings.out()) !=
          errSecSuccess ||
      settings.get() == nullptr) {
    return Decision::None;
  }
  const CFIndex count = CFArrayGetCount(settings.get());
  if (count == 0) {
    return Decision::Trust; // Trusted as a root for every purpose.
  }
  for (CFIndex i = 0; i < count; i++) {
    const auto entry =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(settings.get(), i));
    if (settingResult(entry) == kSecTrustSettingsResultDeny) {
      return Decision::Deny;
    }
  }
  for (CFIndex i = 0; i < count; i++) {
    const auto entry =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(settings.get(), i));
    const auto policy = static_cast<SecPolicyRef>(const_cast<void*>(
        CFDictionaryGetValue(entry, kSecTrustSettingsPolicy)));
    if (policy != nullptr && !isSslPolicy(policy)) {
      continue;
    }
    if (CFDictionaryGetValue(entry, kSecTrustSettingsPolicyString) != nullptr) {
      continue; // Only for some host names.
    }
    const SInt32 result = settingResult(entry);
    if (result == kSecTrustSettingsResultTrustRoot ||
        result == kSecTrustSettingsResultTrustAsRoot) {
      return Decision::Trust;
    }
    if (result == kSecTrustSettingsResultUnspecified) {
      return Decision::None;
    }
  }
  return Decision::None;
}

bool expired(SecCertificateRef certificate) {
  const void* keys[] = {kSecOIDX509V1ValidityNotAfter};
  const CFHolder<CFArrayRef> wanted(
      CFArrayCreate(kCFAllocatorDefault, keys, 1, &kCFTypeArrayCallBacks));
  const CFHolder<CFDictionaryRef> values(
      SecCertificateCopyValues(certificate, wanted.get(), nullptr));
  if (values.get() == nullptr) {
    return false;
  }
  const auto property = static_cast<CFDictionaryRef>(
      CFDictionaryGetValue(values.get(), kSecOIDX509V1ValidityNotAfter));
  if (property == nullptr) {
    return false;
  }
  // An absolute time (seconds since 2001) as a CFNumber.
  const auto notAfter = static_cast<CFNumberRef>(
      CFDictionaryGetValue(property, kSecPropertyKeyValue));
  CFAbsoluteTime time = 0;
  return notAfter != nullptr && CFGetTypeID(notAfter) == CFNumberGetTypeID() &&
         CFNumberGetValue(notAfter, kCFNumberDoubleType, &time) != 0 &&
         time < CFAbsoluteTimeGetCurrent();
}

bool appleTrustsAnchor(SecCertificateRef certificate) {
  static const CFHolder<SecPolicyRef> policy(SecPolicyCreateBasicX509());
  CFHolder<SecTrustRef> trust;
  if (SecTrustCreateWithCertificates(certificate, policy.get(), trust.out()) !=
      errSecSuccess) {
    return false;
  }
  SecTrustSetNetworkFetchAllowed(trust.get(), false);
  return SecTrustEvaluateWithError(trust.get(), nullptr);
}

bool trustedForTls(SecCertificateRef certificate, bool systemAnchor) {
  if (expired(certificate)) {
    return false;
  }
  for (const SecTrustSettingsDomain domain :
       {kSecTrustSettingsDomainUser, kSecTrustSettingsDomainAdmin}) {
    switch (decisionInDomain(certificate, domain)) {
      case Decision::Trust:
        return true;
      case Decision::Deny:
        return false;
      case Decision::None:
        break;
    }
  }
  // No user or admin decision: a system root counts when Apple's own
  // evaluation accepts it. Apple distrusts some roots it still ships as
  // anchors (e.g. TrustCor), and restricts others in the system domain.
  return systemAnchor && appleTrustsAnchor(certificate);
}

std::string derToPem(CFDataRef der) {
  const auto* bytes = CFDataGetBytePtr(der);
  const std::vector<uint8_t> data(bytes, bytes + CFDataGetLength(der));
  const std::string base64 = base64Encode(data);
  std::string pem          = "-----BEGIN CERTIFICATE-----\n";
  for (std::size_t i = 0; i < base64.size(); i += 64) {
    pem += base64.substr(i, 64);
    pem += '\n';
  }
  pem += "-----END CERTIFICATE-----\n";
  return pem;
}

CFIndex countOf(CFArrayRef array) {
  return array != nullptr ? CFArrayGetCount(array) : 0;
}

SystemTrustStore buildSystemTrustStore() {
  CFHolder<CFArrayRef> anchors;
  if (SecTrustCopyAnchorCertificates(anchors.out()) != errSecSuccess ||
      countOf(anchors.get()) == 0) {
    // A bundle without the system roots would make every public server
    // fail: keep libcurl's own bundle instead.
    WriteLog(LL_WARN,
             "  WARNING: Can't read the system root certificates; the "
             "keychains' trust settings are not used");
    return {};
  }
  // errSecNoTrustSettings when a domain has none; the sandbox may also deny
  // access. The system roots still count.
  CFHolder<CFArrayRef> userTrusted;
  CFHolder<CFArrayRef> adminTrusted;
  SecTrustSettingsCopyCertificates(kSecTrustSettingsDomainUser,
                                   userTrusted.out());
  SecTrustSettingsCopyCertificates(kSecTrustSettingsDomainAdmin,
                                   adminTrusted.out());

  SystemTrustStore store;
  std::set<std::string> seen;
  std::size_t rejected = 0;
  const auto consider  = [&](CFArrayRef certificates, bool systemAnchors) {
    for (CFIndex i = 0; i < countOf(certificates); i++) {
      const auto certificate = static_cast<SecCertificateRef>(
          const_cast<void*>(CFArrayGetValueAtIndex(certificates, i)));
      const CFHolder<CFDataRef> der(SecCertificateCopyData(certificate));
      if (der.get() == nullptr) {
        continue;
      }
      std::string key(
          reinterpret_cast<const char*>(CFDataGetBytePtr(der.get())),
          static_cast<std::size_t>(CFDataGetLength(der.get())));
      if (!seen.insert(std::move(key)).second) {
        continue;
      }
      if (!trustedForTls(certificate, systemAnchors)) {
        rejected++;
        continue;
      }
      store.pem += derToPem(der.get());
      store.certificateCount++;
    }
  };
  // Anchors first: a root that is both an anchor and in a trust settings
  // domain keeps its anchor status.
  consider(anchors.get(), true);
  consider(userTrusted.get(), false);
  consider(adminTrusted.get(), false);

  WriteLog(LL_DEBUG,
           "  System trust store: " + std::to_string(store.certificateCount) +
               " certificates (system anchors: " +
               std::to_string(countOf(anchors.get())) +
               ", user trust settings: " +
               std::to_string(countOf(userTrusted.get())) +
               ", admin trust settings: " +
               std::to_string(countOf(adminTrusted.get())) +
               ", not trusted for TLS: " + std::to_string(rejected) + ")");
  return store;
}

} // namespace

const SystemTrustStore& systemTrustStore() {
  static const SystemTrustStore store = buildSystemTrustStore();
  return store;
}

} // namespace platform
