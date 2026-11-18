#pragma once

#include <cstddef>
#include <string>

/*
The certificates the operating system trusts for TLS, for curl backends that
don't read the system trust store themselves. Implementations live in
src/platform/<os>/systemTrustStore.cpp.
*/
namespace platform {

struct SystemTrustStore {
    // PEM bundle of the trusted certificates. Empty when not needed (Windows:
    // Schannel uses the certificate store directly) or not available.
    std::string pem;
    // How many certificates the bundle holds, for logging.
    std::size_t certificateCount = 0;
};

// Computed on the first call and kept for the life of the process.
const SystemTrustStore& systemTrustStore();

} // namespace platform
