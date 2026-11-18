/*
Smoke test for a real server: reads a sample of every table in a schema
through the iODBC driver manager, the way Excel does, and reports every
conversion error with the table, column and type.

Each table is read twice with SELECT * ... LIMIT n:
  1. columns bound to the C types a spreadsheet uses (SQLBindCol + SQLFetch):
     numbers as integer or double, dates/times/timestamps as timestamps,
     booleans as bits, binary data as bytes;
  2. every column as text, in parts (SQLGetData with SQL_C_CHAR).

Usage:
  TrinoOdbcSmoke "DSN=..." --schema catalog.schema[,schema,...] [--limit 100]
                 [--max-tables 50] [--tables t1,t2]

--schema takes a comma-separated list; a schema without "catalog." uses the
catalog before it. --tables needs a single schema.

The connection string may add options such as ansiEncoding=x-mac-cyrillic
to read text the way Excel for Mac with a Russian UI does. Exit code: 0 when
everything converted, 1 otherwise.

Links only libiodbc, never the driver itself.
*/

#include <sql.h>
#include <sqlext.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Column {
    std::string name;
    SQLSMALLINT sqlType = 0;
};

struct Options {
    std::string connection;
    // catalog.schema pairs, read in order over one connection.
    std::vector<std::pair<std::string, std::string>> schemas;
    std::vector<std::string> tables;
    int limit     = 100;
    int maxTables = 50;
};

int problems     = 0;
int serverErrors = 0;

std::string diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
  std::string result;
  for (SQLSMALLINT record = 1;; record++) {
    SQLCHAR state[6]      = {};
    SQLCHAR message[2048] = {};
    SQLINTEGER native     = 0;
    SQLSMALLINT length    = 0;
    if (!SQL_SUCCEEDED(SQLGetDiagRec(type,
                                     handle,
                                     record,
                                     state,
                                     &native,
                                     message,
                                     sizeof(message),
                                     &length))) {
      break;
    }
    if (!result.empty()) {
      result += " | ";
    }
    result += std::string(reinterpret_cast<char*>(state)) + " " +
              reinterpret_cast<char*>(message);
  }
  return result.empty() ? "(no diagnostics)" : result;
}

std::string quoteIdentifier(const std::string& name) {
  std::string quoted = "\"";
  for (const char c : name) {
    quoted += c;
    if (c == '"') {
      quoted += '"';
    }
  }
  return quoted + "\"";
}

std::string typeName(SQLSMALLINT type) {
  switch (type) {
    case SQL_CHAR:
      return "CHAR";
    case SQL_VARCHAR:
      return "VARCHAR";
    case SQL_LONGVARCHAR:
      return "LONGVARCHAR";
    case SQL_WCHAR:
      return "WCHAR";
    case SQL_WVARCHAR:
      return "WVARCHAR";
    case SQL_WLONGVARCHAR:
      return "WLONGVARCHAR";
    case SQL_BIT:
      return "BIT";
    case SQL_TINYINT:
      return "TINYINT";
    case SQL_SMALLINT:
      return "SMALLINT";
    case SQL_INTEGER:
      return "INTEGER";
    case SQL_BIGINT:
      return "BIGINT";
    case SQL_REAL:
      return "REAL";
    case SQL_FLOAT:
      return "FLOAT";
    case SQL_DOUBLE:
      return "DOUBLE";
    case SQL_DECIMAL:
      return "DECIMAL";
    case SQL_NUMERIC:
      return "NUMERIC";
    case SQL_TYPE_DATE:
      return "DATE";
    case SQL_TYPE_TIME:
      return "TIME";
    case SQL_TYPE_TIMESTAMP:
      return "TIMESTAMP";
    case SQL_BINARY:
      return "BINARY";
    case SQL_VARBINARY:
      return "VARBINARY";
    case SQL_LONGVARBINARY:
      return "LONGVARBINARY";
    case SQL_GUID:
      return "GUID";
    default:
      return "type " + std::to_string(type);
  }
}

// The C type a spreadsheet binds a column of this SQL type to; 0 = read it
// as text in the second pass only.
SQLSMALLINT spreadsheetCType(SQLSMALLINT sqlType, SQLLEN& size) {
  switch (sqlType) {
    case SQL_BIT:
      size = sizeof(unsigned char);
      return SQL_C_BIT;
    case SQL_TINYINT:
    case SQL_SMALLINT:
    case SQL_INTEGER:
      size = sizeof(SQLINTEGER);
      return SQL_C_SLONG;
    case SQL_BIGINT:
      size = sizeof(SQLBIGINT);
      return SQL_C_SBIGINT;
    case SQL_REAL:
    case SQL_FLOAT:
    case SQL_DOUBLE:
    case SQL_DECIMAL:
    case SQL_NUMERIC:
      size = sizeof(double);
      return SQL_C_DOUBLE;
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_TYPE_TIMESTAMP:
      size = sizeof(SQL_TIMESTAMP_STRUCT);
      return SQL_C_TYPE_TIMESTAMP;
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
      size = 8192;
      return SQL_C_BINARY;
    default:
      return 0;
  }
}

// The "message:" line of a Trino error, or "" for other diagnostics.
std::string trinoMessage(const std::string& diagnostics) {
  if (diagnostics.find("Trino Error Information") == std::string::npos) {
    return "";
  }
  const std::size_t at = diagnostics.find("message: ");
  if (at == std::string::npos) {
    return "server error";
  }
  const std::size_t end = diagnostics.find('\n', at);
  return diagnostics.substr(
      at + 9, end == std::string::npos ? std::string::npos : end - at - 9);
}

// Server-side query errors (broken table, permissions, ...) aren't driver
// problems: they're listed separately, in one line.
void report(const std::string& table, const std::string& what) {
  const std::string server = trinoMessage(what);
  if (!server.empty()) {
    serverErrors++;
    std::printf("  skip %s: %s\n", table.c_str(), server.c_str());
    return;
  }
  problems++;
  std::printf("  FAIL %s: %s\n", table.c_str(), what.c_str());
}

std::vector<Column> describe(SQLHSTMT stmt) {
  SQLSMALLINT count = 0;
  SQLNumResultCols(stmt, &count);
  std::vector<Column> columns;
  for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(count); i++) {
    SQLCHAR name[512]   = {};
    SQLSMALLINT nameLen = 0;
    Column column;
    SQLULEN size         = 0;
    SQLSMALLINT digits   = 0;
    SQLSMALLINT nullable = 0;
    SQLDescribeCol(stmt,
                   i,
                   name,
                   sizeof(name),
                   &nameLen,
                   &column.sqlType,
                   &size,
                   &digits,
                   &nullable);
    column.name = reinterpret_cast<char*>(name);
    columns.push_back(column);
  }
  return columns;
}

// Adds the column named in a "(column N)" driver message.
std::string withColumn(const std::string& message,
                       const std::vector<Column>& columns) {
  const std::size_t at = message.find("(column ");
  if (at == std::string::npos) {
    return message;
  }
  const int index = std::atoi(message.c_str() + at + 8);
  if (index < 1 || index > static_cast<int>(columns.size())) {
    return message;
  }
  const Column& column = columns[static_cast<std::size_t>(index - 1)];
  return message + " [" + column.name + " " + typeName(column.sqlType) + "]";
}

bool execute(SQLHDBC dbc,
             const std::string& query,
             const std::string& table,
             SQLHSTMT* stmt) {
  SQLAllocHandle(SQL_HANDLE_STMT, dbc, stmt);
  const SQLRETURN rc = SQLExecDirect(*stmt, (SQLCHAR*)query.c_str(), SQL_NTS);
  if (!SQL_SUCCEEDED(rc)) {
    report(table, "query failed: " + diagnostics(SQL_HANDLE_STMT, *stmt));
    SQLFreeHandle(SQL_HANDLE_STMT, *stmt);
    return false;
  }
  return true;
}

// Pass 1: bound columns, the way a spreadsheet loads a table.
int readBound(SQLHDBC dbc, const std::string& query, const std::string& table) {
  SQLHSTMT stmt = SQL_NULL_HSTMT;
  if (!execute(dbc, query, table, &stmt)) {
    return -1;
  }
  const std::vector<Column> columns = describe(stmt);
  std::vector<std::vector<unsigned char>> buffers(columns.size());
  std::vector<SQLLEN> indicators(columns.size(), 0);
  for (std::size_t i = 0; i < columns.size(); i++) {
    SQLLEN size             = 0;
    const SQLSMALLINT cType = spreadsheetCType(columns[i].sqlType, size);
    if (cType == 0) {
      continue;
    }
    buffers[i].resize(static_cast<std::size_t>(size));
    SQLBindCol(stmt,
               static_cast<SQLUSMALLINT>(i + 1),
               cType,
               buffers[i].data(),
               size,
               &indicators[i]);
  }
  int rows = 0;
  for (;;) {
    const SQLRETURN rc = SQLFetch(stmt);
    if (rc == SQL_NO_DATA) {
      break;
    }
    if (!SQL_SUCCEEDED(rc)) {
      report(table,
             "typed fetch, row " + std::to_string(rows + 1) + ": " +
                 withColumn(diagnostics(SQL_HANDLE_STMT, stmt), columns));
      break;
    }
    rows++;
  }
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
  return rows;
}

// Pass 2: every column as text, in 4 KB parts.
void readText(SQLHDBC dbc, const std::string& query, const std::string& table) {
  SQLHSTMT stmt = SQL_NULL_HSTMT;
  if (!execute(dbc, query, table, &stmt)) {
    return;
  }
  const std::vector<Column> columns = describe(stmt);
  int row                           = 0;
  while (true) {
    const SQLRETURN rc = SQLFetch(stmt);
    if (rc == SQL_NO_DATA) {
      break;
    }
    if (!SQL_SUCCEEDED(rc)) {
      report(table,
             "text fetch, row " + std::to_string(row + 1) + ": " +
                 diagnostics(SQL_HANDLE_STMT, stmt));
      break;
    }
    row++;
    for (std::size_t i = 0; i < columns.size(); i++) {
      char part[4096]  = {};
      SQLLEN indicator = 0;
      for (int calls = 0; calls < 100000; calls++) {
        const SQLRETURN got = SQLGetData(stmt,
                                         static_cast<SQLUSMALLINT>(i + 1),
                                         SQL_C_CHAR,
                                         part,
                                         sizeof(part),
                                         &indicator);
        if (got == SQL_SUCCESS || got == SQL_NO_DATA) {
          break;
        }
        if (got != SQL_SUCCESS_WITH_INFO) {
          report(table,
                 "text, row " + std::to_string(row) + ", column " +
                     columns[i].name + " " + typeName(columns[i].sqlType) +
                     ": " + diagnostics(SQL_HANDLE_STMT, stmt));
          break;
        }
      }
    }
  }
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

std::vector<std::string> listTables(SQLHDBC dbc,
                                    const std::string& catalog,
                                    const std::string& schema,
                                    int maxTables) {
  std::vector<std::string> tables;
  SQLHSTMT stmt = SQL_NULL_HSTMT;
  SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
  const SQLRETURN rc = SQLTables(stmt,
                                 (SQLCHAR*)catalog.c_str(),
                                 SQL_NTS,
                                 (SQLCHAR*)schema.c_str(),
                                 SQL_NTS,
                                 (SQLCHAR*)"%",
                                 SQL_NTS,
                                 (SQLCHAR*)"TABLE,VIEW",
                                 SQL_NTS);
  if (!SQL_SUCCEEDED(rc)) {
    std::printf("SQLTables failed: %s\n",
                diagnostics(SQL_HANDLE_STMT, stmt).c_str());
    problems++;
  } else {
    while (SQL_SUCCEEDED(SQLFetch(stmt)) &&
           static_cast<int>(tables.size()) < maxTables) {
      char name[512]   = {};
      SQLLEN indicator = 0;
      SQLGetData(stmt, 3, SQL_C_CHAR, name, sizeof(name), &indicator);
      tables.emplace_back(name);
    }
  }
  SQLFreeHandle(SQL_HANDLE_STMT, stmt);
  return tables;
}

bool parseArguments(int argc, char** argv, Options& options) {
  if (argc < 2) {
    return false;
  }
  options.connection = argv[1];
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string flag  = argv[i];
    const std::string value = argv[i + 1];
    if (flag == "--schema") {
      // "catalog.a,b,other.c": a schema without a catalog takes the
      // previous one.
      std::string catalog;
      std::size_t start = 0;
      while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::string item  = value.substr(
            start,
            comma == std::string::npos ? std::string::npos : comma - start);
        const std::size_t dot = item.find('.');
        if (dot != std::string::npos) {
          catalog = item.substr(0, dot);
          options.schemas.emplace_back(catalog, item.substr(dot + 1));
        } else if (!item.empty()) {
          if (catalog.empty()) {
            return false;
          }
          options.schemas.emplace_back(catalog, item);
        }
        if (comma == std::string::npos) {
          break;
        }
        start = comma + 1;
      }
    } else if (flag == "--limit") {
      options.limit = std::atoi(value.c_str());
    } else if (flag == "--max-tables") {
      options.maxTables = std::atoi(value.c_str());
    } else if (flag == "--tables") {
      std::size_t start = 0;
      while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::string table = value.substr(
            start,
            comma == std::string::npos ? std::string::npos : comma - start);
        if (!table.empty()) {
          options.tables.push_back(table);
        }
        if (comma == std::string::npos) {
          break;
        }
        start = comma + 1;
      }
    } else {
      return false;
    }
  }
  return !options.schemas.empty() &&
         (options.tables.empty() || options.schemas.size() == 1);
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parseArguments(argc, argv, options)) {
    std::fprintf(stderr,
                 "Usage: %s \"DSN=...\" --schema catalog.schema[,schema...] "
                 "[--limit 100] "
                 "[--max-tables 50] [--tables t1,t2]\n",
                 argv[0]);
    return 2;
  }

  SQLHENV env = SQL_NULL_HENV;
  SQLHDBC dbc = SQL_NULL_HDBC;
  SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
  SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
  SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
  if (!SQL_SUCCEEDED(SQLDriverConnect(dbc,
                                      nullptr,
                                      (SQLCHAR*)options.connection.c_str(),
                                      SQL_NTS,
                                      nullptr,
                                      0,
                                      nullptr,
                                      SQL_DRIVER_NOPROMPT))) {
    std::printf("Connect failed: %s\n",
                diagnostics(SQL_HANDLE_DBC, dbc).c_str());
    return 1;
  }

  for (const auto& [catalog, schema] : options.schemas) {
    std::vector<std::string> tables = options.tables;
    if (tables.empty()) {
      tables = listTables(dbc, catalog, schema, options.maxTables);
    }
    std::printf("Checking %zu tables in %s.%s (LIMIT %d)\n",
                tables.size(),
                catalog.c_str(),
                schema.c_str(),
                options.limit);

    for (const std::string& table : tables) {
      const std::string fullName = quoteIdentifier(catalog) + "." +
                                   quoteIdentifier(schema) + "." +
                                   quoteIdentifier(table);
      const std::string query    = "SELECT * FROM " + fullName + " LIMIT " +
                                   std::to_string(options.limit);
      const std::string label    = schema + "." + table;
      const int before           = problems;
      const int serverBefore     = serverErrors;
      const int rows             = readBound(dbc, query, label);
      if (rows >= 0 && serverErrors == serverBefore) {
        readText(dbc, query, label);
      }
      if (problems == before && serverErrors == serverBefore) {
        std::printf("  ok   %s (%d rows)\n", label.c_str(), rows);
      }
      std::fflush(stdout);
    }
  }

  SQLDisconnect(dbc);
  SQLFreeHandle(SQL_HANDLE_DBC, dbc);
  SQLFreeHandle(SQL_HANDLE_ENV, env);
  std::printf("%s: %d driver problem(s), %d table(s) skipped because of "
              "server errors\n",
              problems == 0 ? "PASSED" : "FAILED",
              problems,
              serverErrors);
  return problems == 0 ? 0 : 1;
}
