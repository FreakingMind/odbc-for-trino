#pragma once
#include "windowsLean.hpp"
#include <sql.h>
#include <sqlext.h>

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

#include "../platform/textEncoding.hpp"

using json = nlohmann::json;


// Returns the largest number of bytes that can be copied without splitting a
// character in the target encoding. maxBytes does not include the terminating
// null character.
SQLLEN calculateUtf8SafeCopyLength(const std::string& value,
                                   std::size_t byteOffset,
                                   SQLLEN maxBytes);
// The same for narrow text in the given encoding (platform-specific).
SQLLEN calculateAnsiSafeCopyLength(const std::string& value,
                                   std::size_t byteOffset,
                                   SQLLEN maxBytes,
                                   AnsiEncoding encoding);
// How many bytes of a narrow value to copy into a chunk of maxBytes: whole
// characters, or, when not even one character fits, maxBytes bytes of it
// (splitting the character) so that chunked reads always make progress.
SQLLEN calculateAnsiChunkLength(const std::string& value,
                                std::size_t byteOffset,
                                SQLLEN maxBytes,
                                AnsiEncoding encoding);
SQLLEN calculateWideSafeCopyLengthBytes(const std::wstring& value,
                                        std::size_t byteOffset,
                                        SQLLEN maxBytes);

class ColumnToBufferStatus {
  public:
    bool isSuccess        = false;
    bool isVariableLength = false;
    bool isTruncated      = false;
    // On failure: the SQLSTATE and message for the diagnostic record.
    std::string sqlState;
    std::string errorMessage;

    ColumnToBufferStatus(bool isSuccess,
                         bool isVariableLength,
                         bool isTruncated = false);
    static ColumnToBufferStatus failure(std::string sqlState,
                                        std::string errorMessage);
};

/*
Converts one column of a Trino result row to the C type of an application
buffer (SQLBindCol / SQLGetData). Never throws: conversion failures come back
with the ODBC SQLSTATE (07006 restricted data type, 22003 numeric value out
of range, 22018 invalid character value for cast, ...).

SQL NULL needs strLen_or_IndPtr (22002 otherwise). Fixed-length C types need
a buffer (HY009 otherwise); character types without a buffer only report the
length. SQL_C_CHAR data is converted to ansiEncoding.
*/
ColumnToBufferStatus columnToBuffer(SQLSMALLINT cDataType,
                                    SQLSMALLINT odbcDataType,
                                    const json& rowData,
                                    SQLULEN columnNumber,
                                    void* buffer,
                                    SQLLEN bufferLength,
                                    SQLLEN* strLen_or_IndPtr,
                                    SQLCHAR precision,
                                    SQLCHAR scale,
                                    AnsiEncoding ansiEncoding);
