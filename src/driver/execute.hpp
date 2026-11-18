#pragma once

#include <string>

#include "../platform/textEncoding.hpp"
#include "handles/descriptorHandle.hpp"

// Render a bound parameter as a SQL literal for client-side substitution
// into a prepared statement (strings are quoted and escaped). SQL_C_CHAR
// values are in ansiEncoding, like SQL_C_CHAR result data.
std::string descriptorFieldToParameterText(const DescriptorField& field,
                                           AnsiEncoding ansiEncoding);
