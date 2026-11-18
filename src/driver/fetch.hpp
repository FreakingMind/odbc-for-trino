#pragma once

#include "../util/windowsLean.hpp"
#include <sql.h>

#include "handles/statementHandle.hpp"

// Advances the cursor by one row and fills the bound columns. Shared by
// SQLFetch, SQLFetchScroll and SQLExtendedFetch; the caller clears the
// diagnostics and reports row counts and statuses.
SQLRETURN fetchNextRow(Statement* statement);
