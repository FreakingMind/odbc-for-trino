#include "tlsOptions.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "../platform/systemTrustStore.hpp"
#include "../util/writeLog.hpp"

SslVerification parseSslVerification(const std::string& value) {
  std::string upper = value;
  std::transform(
      upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
      });
  if (upper.empty() || upper == "FULL") {
    return SslVerification::Full;
  }
  if (upper == "CA") {
    return SslVerification::Ca;
  }
  if (upper == "NONE") {
    return SslVerification::None;
  }
  throw std::invalid_argument("Invalid sslVerification value '" + value +
                              "': expected FULL, CA or NONE");
}

std::string toString(SslVerification verification) {
  switch (verification) {
    case SslVerification::Ca:
      return "CA";
    case SslVerification::None:
      return "NONE";
    case SslVerification::Full:
    default:
      return "FULL";
  }
}

namespace {

// Does libcurl verify with OpenSSL or LibreSSL, which read only their own CA
// bundle? libcurl's version text lists the TLS backends, inactive ones in
// parentheses: "(SecureTransport) LibreSSL/3.3.6" on macOS.
bool curlBackendNeedsCaBundle() {
  static const bool needs = [] {
    const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    if (info == nullptr || info->ssl_version == nullptr) {
      return false;
    }
    std::string active;
    int depth = 0;
    for (const char* c = info->ssl_version; *c != '\0'; c++) {
      if (*c == '(') {
        depth++;
      } else if (*c == ')') {
        depth = depth > 0 ? depth - 1 : 0;
      } else if (depth == 0) {
        active += *c;
      }
    }
    return active.find("LibreSSL") != std::string::npos ||
           active.find("OpenSSL") != std::string::npos;
  }();
  return needs;
}

} // namespace

void applyTlsOptions(CURL* curl, const TlsOptions& options) {
  switch (options.verification) {
    case SslVerification::Full:
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
      break;
    case SslVerification::Ca:
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
      break;
    case SslVerification::None:
      WriteLog(LL_WARN,
               "  WARNING: TLS certificate verification is disabled "
               "(sslVerification=NONE)");
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
      break;
  }
  if (!options.caFile.empty()) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, options.caFile.c_str());
    return;
  }
  if (options.verification == SslVerification::None ||
      !curlBackendNeedsCaBundle()) {
    return;
  }
  // No sslCaFile: trust what the operating system trusts. On macOS that
  // includes roots added to the keychains (corporate, MDM), which libcurl's
  // LibreSSL backend wouldn't see on its own.
  const platform::SystemTrustStore& store = platform::systemTrustStore();
  if (store.pem.empty()) {
    return;
  }
  curl_blob blob{};
  blob.data  = const_cast<char*>(store.pem.data());
  blob.len   = store.pem.size();
  blob.flags = CURL_BLOB_NOCOPY; // The store lives for the whole process.
  const CURLcode rc = curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
  if (rc != CURLE_OK) {
    WriteLog(LL_WARN,
             std::string("  WARNING: libcurl can't use the system trust "
                         "store (") +
                 curl_easy_strerror(rc) +
                 "); only its own CA bundle is trusted");
  }
}
