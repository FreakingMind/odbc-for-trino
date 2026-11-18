#include "../../util/cryptUtils.hpp"

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonRandom.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../util/base64.hpp"
#include "../../util/writeLog.hpp"

/*
macOS implementation of the secret encryption helpers.

Windows uses DPAPI, which has no direct equivalent on macOS. Instead, a random
256-bit master key is kept in the current user's login keychain as a generic
password item and created on first use. Secrets are encrypted with
AES-256-CBC (PKCS7) and authenticated with HMAC-SHA256 (encrypt-then-MAC),
using separate encryption and MAC subkeys derived from the master key.

The legacy (file-based) keychain is used on purpose: the data protection
keychain requires the hosting application to carry keychain entitlements,
and this library is loaded into third-party hosts (Excel, CLI tools).

Blob layout (before base64 encoding):

  magic   "TOSB"                4 bytes
  version 0x01                  1 byte
  iv                            16 bytes
  ciphertext                    n * 16 bytes (n >= 1)
  hmac    HMAC-SHA256           32 bytes

The HMAC covers magic, version, iv and ciphertext.

"System" level secrets: macOS has no machine-wide secret store that is
usable without root privileges. System level secrets therefore use a
separate master key, but that key still lives in the *current user's* login
keychain. A System DSN whose client secret was encrypted by one user cannot be
decrypted by another user. Multi-user System DSNs with encrypted secrets are
not supported on macOS.
*/

namespace {
constexpr const char* DEFAULT_KEYCHAIN_SERVICE = "TrinoODBC";
// The keychain service name can be overridden with TRINO_ODBC_KEYCHAIN_SERVICE
// so tests never create or read the real driver's items.
const std::string& keychainService() {
  static const std::string service = [] {
    const char* env = std::getenv("TRINO_ODBC_KEYCHAIN_SERVICE");
    return std::string(env && *env ? env : DEFAULT_KEYCHAIN_SERVICE);
  }();
  return service;
}

constexpr const char* KEYCHAIN_ACCOUNT_USER   = "secret-encryption-key-user";
constexpr const char* KEYCHAIN_ACCOUNT_SYSTEM = "secret-encryption-key-system";
constexpr const char* KEYCHAIN_LABEL = "Trino ODBC secret encryption key";

constexpr std::array<uint8_t, 4> BLOB_MAGIC = {'T', 'O', 'S', 'B'};
constexpr uint8_t BLOB_VERSION              = 0x01;

constexpr size_t MASTER_KEY_SIZE = 32;
constexpr size_t AES_KEY_SIZE    = kCCKeySizeAES256;
constexpr size_t IV_SIZE         = kCCBlockSizeAES128;
constexpr size_t MAC_SIZE        = CC_SHA256_DIGEST_LENGTH;
constexpr size_t HEADER_SIZE     = BLOB_MAGIC.size() + 1;
constexpr size_t MIN_BLOB_SIZE =
    HEADER_SIZE + IV_SIZE + kCCBlockSizeAES128 + MAC_SIZE;

using Bytes = std::vector<uint8_t>;

[[noreturn]] void fail(const std::string& message) {
  WriteLog(LL_ERROR, message);
  throw std::runtime_error(message);
}

// Overwrites key material before it is released.
void wipe(Bytes& data) {
  volatile uint8_t* ptr = data.data();
  for (size_t i = 0; i < data.size(); i++) {
    ptr[i] = 0;
  }
}

struct WipeOnExit {
    Bytes& data;
    ~WipeOnExit() {
      wipe(data);
    }
};

// RAII owner for Core Foundation objects.
template <typename T> class CFRef {
  public:
    explicit CFRef(T ref = nullptr) : ref_(ref) {}
    ~CFRef() {
      if (ref_) {
        CFRelease(ref_);
      }
    }
    CFRef(CFRef&& other) noexcept : ref_(other.ref_) {
      other.ref_ = nullptr;
    }
    CFRef(const CFRef&)            = delete;
    CFRef& operator=(const CFRef&) = delete;
    CFRef& operator=(CFRef&&)      = delete;
    T get() const {
      return ref_;
    }
    T* out() {
      return &ref_;
    }

  private:
    T ref_;
};

std::string cfStringToStd(CFStringRef str) {
  if (!str) {
    return "";
  }
  if (const char* fast = CFStringGetCStringPtr(str, kCFStringEncodingUTF8)) {
    return fast;
  }
  CFIndex length = CFStringGetLength(str);
  CFIndex maxSize =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string buffer(static_cast<size_t>(maxSize), '\0');
  if (!CFStringGetCString(str, buffer.data(), maxSize, kCFStringEncodingUTF8)) {
    return "";
  }
  buffer.resize(std::strlen(buffer.c_str()));
  return buffer;
}

std::string describeStatus(OSStatus status) {
  CFRef<CFStringRef> message(SecCopyErrorMessageString(status, nullptr));
  std::string text   = cfStringToStd(message.get());
  std::string result = "OSStatus " + std::to_string(status);
  if (!text.empty()) {
    result += " (" + text + ")";
  }
  return result;
}

CFRef<CFStringRef> makeCFString(const char* value) {
  CFRef<CFStringRef> str(CFStringCreateWithCString(
      kCFAllocatorDefault, value, kCFStringEncodingUTF8));
  if (!str.get()) {
    fail("Failed to create CFString for keychain query");
  }
  return str;
}

CFRef<CFMutableDictionaryRef> makeKeychainQuery(const char* account) {
  CFRef<CFMutableDictionaryRef> query(
      CFDictionaryCreateMutable(kCFAllocatorDefault,
                                0,
                                &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks));
  if (!query.get()) {
    fail("Failed to create keychain query dictionary");
  }
  CFRef<CFStringRef> service = makeCFString(keychainService().c_str());
  CFRef<CFStringRef> acct    = makeCFString(account);
  CFDictionarySetValue(query.get(), kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query.get(), kSecAttrService, service.get());
  CFDictionarySetValue(query.get(), kSecAttrAccount, acct.get());
  return query;
}

void randomBytes(uint8_t* out, size_t size) {
  CCRNGStatus status = CCRandomGenerateBytes(out, size);
  if (status != kCCSuccess) {
    fail("Failed to generate random bytes: CCRNGStatus " +
         std::to_string(status));
  }
}

// Returns errSecSuccess and fills `key`, or errSecItemNotFound.
// Any other status is an error and throws.
OSStatus readMasterKey(const char* account, Bytes& key) {
  CFRef<CFMutableDictionaryRef> query = makeKeychainQuery(account);
  CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);

  CFRef<CFTypeRef> result;
  OSStatus status = SecItemCopyMatching(query.get(), result.out());
  if (status == errSecItemNotFound) {
    return status;
  }
  if (status != errSecSuccess) {
    fail("Failed to read encryption key '" + std::string(account) +
         "' from the keychain: " + describeStatus(status));
  }
  if (!result.get() || CFGetTypeID(result.get()) != CFDataGetTypeID()) {
    fail("Keychain returned unexpected data for encryption key '" +
         std::string(account) + "'");
  }
  CFDataRef data = static_cast<CFDataRef>(result.get());
  CFIndex length = CFDataGetLength(data);
  if (length != static_cast<CFIndex>(MASTER_KEY_SIZE)) {
    fail("Encryption key '" + std::string(account) +
         "' in the keychain has invalid length " + std::to_string(length) +
         " (expected " + std::to_string(MASTER_KEY_SIZE) + "). Delete the '" +
         keychainService() +
         "' keychain item to reset it; previously encrypted secrets will "
         "have to be entered again.");
  }
  const UInt8* ptr = CFDataGetBytePtr(data);
  key.assign(ptr, ptr + length);
  return errSecSuccess;
}

// Returns errSecSuccess or errSecDuplicateItem. Any other status throws.
OSStatus addMasterKey(const char* account, const Bytes& key) {
  CFRef<CFMutableDictionaryRef> query = makeKeychainQuery(account);
  CFRef<CFStringRef> label            = makeCFString(KEYCHAIN_LABEL);
  CFRef<CFDataRef> data(CFDataCreate(
      kCFAllocatorDefault, key.data(), static_cast<CFIndex>(key.size())));
  if (!data.get()) {
    fail("Failed to create CFData for keychain item");
  }
  CFDictionarySetValue(query.get(), kSecAttrLabel, label.get());
  CFDictionarySetValue(query.get(), kSecValueData, data.get());

  /*
  No kSecAttrAccess: the default ACL trusts the application that creates the
  item (inside Excel: Excel), so the host reads the key back without a
  prompt. Other applications get the standard macOS consent dialog, which is
  the intended behavior: there is no supported way to make a keychain item
  silently readable by arbitrary apps.
  */
  OSStatus status = SecItemAdd(query.get(), nullptr);
  if (status != errSecSuccess && status != errSecDuplicateItem) {
    fail("Failed to store encryption key '" + std::string(account) +
         "' in the keychain: " + describeStatus(status));
  }
  return status;
}

/*
Fail early, without UI, if there is no usable default keychain. Otherwise
SecItemAdd shows a modal "Keychain Not Found ... Reset To Defaults" dialog,
which a library loaded into someone else's app (Excel) must never do, and
whose "Reset To Defaults" would wipe the user's login keychain. This
happens e.g. when $HOME doesn't point at the real home directory.
The SecKeychain* calls are deprecated but are the only way to inspect the
legacy keychain search list; they don't show any UI themselves.
*/
void requireDefaultKeychain() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  SecKeychainRef keychain          = nullptr;
  OSStatus status                  = SecKeychainCopyDefault(&keychain);
  SecKeychainStatus keychainStatus = 0;
  if (status == errSecSuccess && keychain) {
    status = SecKeychainGetStatus(keychain, &keychainStatus);
  }
  if (keychain) {
    CFRelease(keychain);
  }
#pragma clang diagnostic pop
  if (status != errSecSuccess) {
    fail("No usable default keychain (" + describeStatus(status) +
         "); not storing the encryption key");
  }
}

Bytes loadOrCreateMasterKey(const char* account) {
  requireDefaultKeychain();
  Bytes key;
  if (readMasterKey(account, key) == errSecSuccess) {
    return key;
  }

  Bytes newKey(MASTER_KEY_SIZE);
  WipeOnExit wipeNewKey{newKey};
  randomBytes(newKey.data(), newKey.size());
  if (addMasterKey(account, newKey) == errSecSuccess) {
    WriteLog(LL_INFO,
             "Created encryption key '" + std::string(account) +
                 "' in the keychain");
    return newKey;
  }

  // Another process created the key between our read and add: use theirs.
  if (readMasterKey(account, key) != errSecSuccess) {
    fail("Encryption key '" + std::string(account) +
         "' reported as duplicate but could not be read from the keychain");
  }
  return key;
}

// Master keys are cached per process so the keychain is consulted only once.
const Bytes& getMasterKey(const char* account) {
  static std::mutex mutex;
  static std::map<std::string, Bytes> cache;

  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(account);
  if (it == cache.end()) {
    it = cache.emplace(account, loadOrCreateMasterKey(account)).first;
  }
  return it->second;
}

Bytes hmacSha256(const Bytes& key, const uint8_t* data, size_t size) {
  Bytes mac(MAC_SIZE);
  CCHmac(kCCHmacAlgSHA256, key.data(), key.size(), data, size, mac.data());
  return mac;
}

struct SubKeys {
    Bytes enc;
    Bytes mac;
    ~SubKeys() {
      wipe(enc);
      wipe(mac);
    }
};

SubKeys deriveSubKeys(const Bytes& masterKey) {
  static constexpr char ENC_LABEL[] = "TrinoODBC secret v1 enc";
  static constexpr char MAC_LABEL[] = "TrinoODBC secret v1 mac";
  SubKeys keys;
  keys.enc = hmacSha256(masterKey,
                        reinterpret_cast<const uint8_t*>(ENC_LABEL),
                        sizeof(ENC_LABEL) - 1);
  keys.mac = hmacSha256(masterKey,
                        reinterpret_cast<const uint8_t*>(MAC_LABEL),
                        sizeof(MAC_LABEL) - 1);
  return keys;
}

bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t size) {
  uint8_t diff = 0;
  for (size_t i = 0; i < size; i++) {
    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  }
  return diff == 0;
}

std::string encryptString(const std::string& text, const char* account) {
  if (text.empty()) {
    return "";
  }
  SubKeys keys = deriveSubKeys(getMasterKey(account));

  Bytes blob(BLOB_MAGIC.begin(), BLOB_MAGIC.end());
  blob.push_back(BLOB_VERSION);

  size_t ivOffset = blob.size();
  blob.resize(ivOffset + IV_SIZE);
  randomBytes(blob.data() + ivOffset, IV_SIZE);

  size_t ctOffset   = blob.size();
  size_t ctCapacity = text.size() + kCCBlockSizeAES128;
  blob.resize(ctOffset + ctCapacity);
  size_t ctLength        = 0;
  CCCryptorStatus status = CCCrypt(kCCEncrypt,
                                   kCCAlgorithmAES,
                                   kCCOptionPKCS7Padding,
                                   keys.enc.data(),
                                   AES_KEY_SIZE,
                                   blob.data() + ivOffset,
                                   text.data(),
                                   text.size(),
                                   blob.data() + ctOffset,
                                   ctCapacity,
                                   &ctLength);
  if (status != kCCSuccess) {
    fail("Encryption failed: CCCryptorStatus " + std::to_string(status));
  }
  blob.resize(ctOffset + ctLength);

  Bytes mac = hmacSha256(keys.mac, blob.data(), blob.size());
  blob.insert(blob.end(), mac.begin(), mac.end());

  return base64Encode(blob);
}

std::string decryptString(const std::string& text, const char* account) {
  if (text.empty()) {
    return "";
  }

  Bytes blob;
  try {
    blob = base64Decode(text);
  } catch (const std::exception& e) {
    fail(std::string("Decryption failed: encrypted secret is not valid "
                     "base64: ") +
         e.what());
  }

  if (blob.size() < MIN_BLOB_SIZE ||
      !std::equal(BLOB_MAGIC.begin(), BLOB_MAGIC.end(), blob.begin())) {
    fail("Decryption failed: encrypted secret has an unrecognized format");
  }
  if (blob[BLOB_MAGIC.size()] != BLOB_VERSION) {
    fail("Decryption failed: unsupported encrypted secret version " +
         std::to_string(blob[BLOB_MAGIC.size()]));
  }
  size_t ivOffset  = HEADER_SIZE;
  size_t ctOffset  = ivOffset + IV_SIZE;
  size_t macOffset = blob.size() - MAC_SIZE;
  size_t ctLength  = macOffset - ctOffset;
  if (ctLength % kCCBlockSizeAES128 != 0) {
    fail("Decryption failed: encrypted secret has an invalid length");
  }

  SubKeys keys      = deriveSubKeys(getMasterKey(account));
  Bytes expectedMac = hmacSha256(keys.mac, blob.data(), macOffset);
  if (!constantTimeEqual(
          expectedMac.data(), blob.data() + macOffset, MAC_SIZE)) {
    fail("Decryption failed: integrity check failed (the secret was "
         "modified, or was encrypted with a different key or by a "
         "different user)");
  }

  Bytes plain(ctLength);
  WipeOnExit wipePlain{plain};
  size_t plainLength     = 0;
  CCCryptorStatus status = CCCrypt(kCCDecrypt,
                                   kCCAlgorithmAES,
                                   kCCOptionPKCS7Padding,
                                   keys.enc.data(),
                                   AES_KEY_SIZE,
                                   blob.data() + ivOffset,
                                   blob.data() + ctOffset,
                                   ctLength,
                                   plain.data(),
                                   plain.size(),
                                   &plainLength);
  if (status != kCCSuccess) {
    fail("Decryption failed: CCCryptorStatus " + std::to_string(status));
  }
  return std::string(reinterpret_cast<const char*>(plain.data()), plainLength);
}
} // namespace

std::string userEncryptString(const std::string& text) {
  // Encrypt a string with the current user's key.
  return encryptString(text, KEYCHAIN_ACCOUNT_USER);
}

std::string userDecryptString(const std::string& text) {
  // Decrypt a string with the current user's key.
  return decryptString(text, KEYCHAIN_ACCOUNT_USER);
}

std::string systemEncryptString(const std::string& text) {
  // Encrypt a string with the "system" key. On macOS this key is still stored
  // in the current user's login keychain; see the comment at the top.
  return encryptString(text, KEYCHAIN_ACCOUNT_SYSTEM);
}

std::string systemDecryptString(const std::string& text) {
  // Decrypt a string with the "system" key (bound to the current user).
  return decryptString(text, KEYCHAIN_ACCOUNT_SYSTEM);
}
