#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "../../trinoAPIWrapper/apiAuthMethod.hpp"
#include "../../util/writeLog.hpp"


extern std::map<std::string, std::string> DRIVER_CONFIG_DEFAULT_VALUES;
extern std::vector<std::string> LOG_LEVEL_NAMES;
extern std::vector<std::string> AUTH_METHOD_NAMES;

class DriverConfig {
  private:
    // Actual configuration values.
    std::string dsn              = "";
    std::string driver           = "";
    std::string hostname         = "";
    uint16_t port                = 0;
    LogLevel logLevel            = LL_NONE;
    ApiAuthMethod authMethod     = AM_NO_AUTH;
    std::string oidcDiscoveryUrl = "";
    std::string clientId         = "";
    std::string clientSecret     = "";
    std::string oidcScope        = "";
    std::string tokenEndpoint    = "";
    std::string grantType        = "";
    std::string sslVerification  = "FULL";
    std::string sslCaFile        = "";
    std::string ansiEncoding     = "auto";
    std::string catalog          = "";
    std::string schema           = "";

    // Metadata describing the status of this config object.
    bool isSaved = false;

  public:
    std::string getDSN();
    void setDSN(std::string dsn);

    std::string getDriver();
    void setDriver(std::string driver);

    std::string getHostname();
    void setHostname(std::string hostname);

    std::string getPortStr();
    uint16_t getPortNum();
    void setPort(std::string port);
    void setPort(uint16_t port);

    std::string getLogLevelStr();
    LogLevel getLogLevelEnum();
    void setLogLevel(LogLevel level);
    void setLogLevel(std::string level);

    std::string getAuthMethodStr();
    ApiAuthMethod getAuthMethodEnum();
    void setAuthMethod(std::string authMethod);
    void setAuthMethod(ApiAuthMethod authMethod);

    std::string getOidcDiscoveryUrl();
    void setOidcDiscoveryUrl(std::string oidcDiscoveryUrl);

    std::string getClientId();
    void setClientId(std::string clientId);

    std::string getClientSecret();
    void setClientSecret(std::string clientSecret);

    std::string getOidcScope();
    void setOidcScope(std::string oidcScope);

    bool getIsSaved();
    void setIsSaved(bool isSaved);

    std::string getTokenEndpoint();
    void setTokenEndpoint(std::string tokenEndpoint);

    std::string getGrantType();
    void setGrantType(std::string grantType);

    // TLS: FULL | CA | NONE (validated by the setter) and a PEM CA bundle.
    std::string getSslVerification();
    void setSslVerification(std::string sslVerification);
    std::string getSslCaFile();
    void setSslCaFile(std::string sslCaFile);

    // Default catalog and schema of the session (X-Trino-Catalog /
    // X-Trino-Schema): unqualified table names resolve against them.
    std::string getCatalog();
    void setCatalog(std::string catalog);
    std::string getSchema();
    void setSchema(std::string schema);

    // Narrow (SQL_C_CHAR) data encoding: auto | UTF-8 | IANA name (macOS).
    // See resolveAnsiEncoding in src/platform/textEncoding.hpp.
    std::string getAnsiEncoding();
    void setAnsiEncoding(std::string ansiEncoding);

    std::string serialize();
    static DriverConfig deserialize(const std::string& jsonStr);
};

DriverConfig driverConfigFromKVPs(std::map<std::string, std::string> kvps);
std::map<std::string, std::string> driverConfigToKVPs(DriverConfig config);
