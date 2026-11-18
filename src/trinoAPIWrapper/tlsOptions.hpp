#pragma once

#include <string>

#include <curl/curl.h>

/*
TLS settings for connections to Trino and to the OAuth2 identity provider.

Modeled on the Trino JDBC driver's SSLVerification property:
- FULL: verify the certificate chain and that it matches the host name
        (default).
- CA:   verify the certificate chain only.
- NONE: no verification at all. Only for testing / closed networks.

caFile optionally names a PEM bundle of trusted CA certificates, e.g. for
a self-signed test server; it replaces the system trust store. When empty,
the operating system's trust store is used: the Windows certificate store
(Schannel), or on macOS the certificates the keychains trust for TLS,
including roots added by the user, an administrator or an MDM profile
(see src/platform/mac/systemTrustStore.cpp).
*/
enum class SslVerification {
  Full,
  Ca,
  None,
};

struct TlsOptions {
    SslVerification verification = SslVerification::Full;
    std::string caFile;
};

// Case-insensitive "FULL" / "CA" / "NONE"; empty means FULL.
// Throws std::invalid_argument for anything else.
SslVerification parseSslVerification(const std::string& value);

std::string toString(SslVerification verification);

// Applies the options to a curl handle.
void applyTlsOptions(CURL* curl, const TlsOptions& options);
