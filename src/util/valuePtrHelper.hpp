#pragma once

#include "windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>

/*
Write a string as a null-terminated character string to the buffer at
ValuePtr, which is BufferLength bytes long (including the terminator), and
the full length of the string in bytes (excluding the terminator) to
StringLengthPtr.

A string that doesn't fit is truncated, at a UTF-8 character boundary, and
still terminated; the function then returns true, and callers report
SQL_SUCCESS_WITH_INFO with SQLSTATE 01004 (string data, right truncated).

If ValuePtr is null, only the length is written. Applications do this to
find out the size of the buffer they need; it's not a truncation.
*/
template <class T>
bool writeNullTermStringToPtr(SQLPOINTER ValuePtr,
                              const std::string& s,
                              SQLLEN BufferLength,
                              T* StringLengthPtr) {
  const std::size_t length = s.length();
  bool truncated           = false;
  if (ValuePtr) {
    if (BufferLength <= 0) {
      truncated = true;
    } else {
      std::size_t copyLength = length;
      if (length >= static_cast<std::size_t>(BufferLength)) {
        truncated  = true;
        copyLength = static_cast<std::size_t>(BufferLength) - 1;
        // Don't cut a multi-byte UTF-8 character in half.
        while (copyLength > 0 &&
               (static_cast<unsigned char>(s[copyLength]) & 0xC0) == 0x80) {
          --copyLength;
        }
      }
      char* valueChars = reinterpret_cast<char*>(ValuePtr);
      std::memcpy(valueChars, s.data(), copyLength);
      valueChars[copyLength] = '\0';
    }
  }
  if (StringLengthPtr) {
    *StringLengthPtr = static_cast<T>(std::min<std::size_t>(
        length, static_cast<std::size_t>(std::numeric_limits<T>::max())));
  }
  return truncated;
}
