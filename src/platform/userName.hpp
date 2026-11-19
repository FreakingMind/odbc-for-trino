#pragma once

#include <string>

namespace platform {

// Login name of the user running the application, used as the Trino user
// when the data source doesn't set one (like the Trino CLI does). Empty if
// it can't be determined.
std::string currentUserName();

} // namespace platform
