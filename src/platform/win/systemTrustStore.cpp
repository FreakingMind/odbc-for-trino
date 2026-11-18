#include "../systemTrustStore.hpp"

namespace platform {

// libcurl uses Schannel on Windows, which verifies against the Windows
// certificate store itself (CURLSSLOPT_NATIVE_CA): nothing to export.
const SystemTrustStore& systemTrustStore() {
  static const SystemTrustStore empty;
  return empty;
}

} // namespace platform
