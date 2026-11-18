#pragma once

#include <cstdint>
#include <string>

#include "../textEncoding.hpp"

/*
macOS details of the ansiEncoding "auto" resolution, exposed for tests.
See ansiEncoding.cpp.
*/

// The Windows code page number of the Mac encoding Microsoft Office uses for
// a UI language ("ru" -> 10007 Mac Cyrillic, "en" -> 10000 Mac Roman).
// Takes a BCP 47 / Apple language tag such as "zh-Hant" or "pt_BR".
std::uint32_t macCodePageForLanguage(const std::string& languageTag);

// Is this the bundle identifier of an Office application that reads
// SQL_C_CHAR data in its UI language's Mac encoding (Excel, Word,
// PowerPoint)? Other Microsoft apps (VS Code, Edge, Teams) are not.
bool isMicrosoftOfficeBundle(const std::string& bundleIdentifier);

// What "auto" resolves to in the application with this bundle identifier
// and UI language (a BCP 47 / Apple language tag).
AnsiEncoding autoAnsiEncoding(const std::string& bundleIdentifier,
                              const std::string& uiLanguage);
