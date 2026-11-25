#include "connHandle.hpp"

#include "../../platform/textEncoding.hpp"
#include "../../trinoAPIWrapper/authProvider/authCredentials.hpp"
#include "../../util/writeLog.hpp"

Connection::Connection(EnvironmentConfig* environmentConfig) {
  this->environmentConfig = environmentConfig;
}

Connection::~Connection() {
  this->freeAllStatements();
  if (this->connectionConfig) {
    delete this->connectionConfig;
  }
}

void Connection::registerStatement(Statement* statement) {
  std::lock_guard<std::mutex> lock(this->statementsMutex);
  this->statements.push_back(statement);
}

void Connection::unregisterStatement(Statement* statement) {
  std::lock_guard<std::mutex> lock(this->statementsMutex);
  std::erase(this->statements, statement);
}

void Connection::freeAllStatements() {
  std::vector<Statement*> remaining;
  {
    std::lock_guard<std::mutex> lock(this->statementsMutex);
    remaining.swap(this->statements);
  }
  for (Statement* statement : remaining) {
    // Cancels a running query; never throws.
    statement->terminate();
    delete statement;
  }
}

void Connection::disconnect() {
  // Statements the application didn't free: stop their queries and free
  // them before the connection they point to goes away.
  this->freeAllStatements();
  // Nothing to tear down if configuring the connection failed.
  if (this->connectionConfig == nullptr) {
    return;
  }
  this->connectionConfig->disconnect();
}

std::string Connection::getServerVersion() {
  if (this->connectionConfig == nullptr) {
    return "";
  }
  return this->connectionConfig->getTrinoServerVersion();
}

void Connection::checkInputs(DriverConfig config) {
  if (config.getHostname().empty()) {
    throw std::invalid_argument("hostname cannot be empty");
  } else if (config.getPortNum() <= 0) {
    throw std::invalid_argument("port must be greater than zero");
  } else if (!config.getSchema().empty() && config.getCatalog().empty() &&
             this->pendingCatalog.empty()) {
    throw std::invalid_argument("schema needs a catalog: set catalog too");
  }
}

void Connection::configure(DriverConfig config) {
  // This instantiates a driver config object on the heap.
  // The destructor will clean it up if that's happened.
  checkInputs(config);
  // Throws on an unknown name, before anything is allocated.
  this->ansiEncoding = resolveAnsiEncoding(config.getAnsiEncoding());
  WriteLog(LL_DEBUG,
           "  Narrow (SQL_C_CHAR) encoding: " +
               ansiEncodingName(this->ansiEncoding) +
               " (ansiEncoding=" + config.getAnsiEncoding() + ")");

  this->connectionConfig = new ConnectionConfig(
      config.getHostname(),
      config.getPortNum(),
      config.getAuthMethodEnum(),
      config.getDSN(),
      config.getOidcDiscoveryUrl(),
      config.getClientId(),
      config.getClientSecret(),
      config.getOidcScope(),
      config.getGrantType(),
      config.getTokenEndpoint(),
      AuthCredentials{.user        = config.getUser(),
                      .password    = config.getPassword(),
                      .accessToken = config.getAccessToken()});

  // Default catalog and schema of the session.
  const std::string catalog =
      this->pendingCatalog.empty() ? config.getCatalog() : this->pendingCatalog;
  if (!catalog.empty()) {
    this->connectionConfig->setRequestHeader(TRINO_CATALOG_HEADER, catalog);
  }
  if (!config.getSchema().empty()) {
    this->connectionConfig->setRequestHeader(TRINO_SCHEMA_HEADER,
                                             config.getSchema());
  }

  TlsOptions tls;
  tls.verification = parseSslVerification(config.getSslVerification());
  tls.caFile       = config.getSslCaFile();
  this->connectionConfig->setTlsOptions(tls);
  this->connectionConfig->setTokenLifetime(config.getTokenLifetime());
}

std::string Connection::getCurrentCatalog() {
  if (this->connectionConfig == nullptr) {
    return this->pendingCatalog;
  }
  return this->connectionConfig->getRequestHeader(TRINO_CATALOG_HEADER);
}

void Connection::setCurrentCatalog(const std::string& catalog) {
  if (this->connectionConfig == nullptr) {
    this->pendingCatalog = catalog;
    return;
  }
  if (catalog.empty()) {
    this->connectionConfig->clearRequestHeader(TRINO_CATALOG_HEADER);
  } else {
    this->connectionConfig->setRequestHeader(TRINO_CATALOG_HEADER, catalog);
  }
}

void Connection::setError(ErrorInfo errorInfo) {
  this->errorInfo = errorInfo;
}

ErrorInfo Connection::getError() {
  return this->errorInfo;
}
