#pragma once

#include <chrono>
#include <string>

/*
Convert a wall-clock time in an IANA time zone (e.g. "Europe/Moscow") to UTC.

Uses std::chrono's time zone database where the standard library provides
one (MSVC). Apple's libc++ does not implement std::chrono::tzdb, so there we
fall back to Howard Hinnant's date library, which std::chrono's design is
based on and which reads the OS zoneinfo database.

Throws std::runtime_error if the zone is unknown.
*/
std::chrono::sys_time<std::chrono::nanoseconds>
localTimeToUtc(const std::string& tzName,
               std::chrono::local_time<std::chrono::nanoseconds> localTime);
