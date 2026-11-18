#include "timezone.hpp"

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>

#if defined(TRINO_ODBC_USE_DATE_TZ)
#include <date/tz.h>
namespace tz {
using date::locate_zone;
using date::time_zone;
using date::zoned_time;
} // namespace tz
#else
namespace tz {
using std::chrono::locate_zone;
using std::chrono::time_zone;
using std::chrono::zoned_time;
} // namespace tz
#endif

namespace {

/*
Since we will be looking up a lot of timezones, cache the lookups.
Time zone objects live for the whole program in both implementations.
*/
const tz::time_zone* getTimezone(const std::string& tzName) {
  static std::map<std::string, const tz::time_zone*> cache;
  static std::mutex cacheMutex;

  std::lock_guard<std::mutex> lock(cacheMutex);
  auto it = cache.find(tzName);
  if (it != cache.end()) {
    return it->second;
  }
  const tz::time_zone* zone = nullptr;
  try {
    zone = tz::locate_zone(tzName);
  } catch (const std::exception& e) {
    throw std::runtime_error("Unknown time zone '" + tzName + "': " + e.what());
  }
  cache.emplace(tzName, zone);
  return zone;
}


// "+03:00" / "-05:30": Trino prints offset zones this way, and they aren't
// in the time zone database.
std::optional<std::chrono::minutes> parseUtcOffset(const std::string& tzName) {
  if (tzName.size() != 6 || (tzName[0] != '+' && tzName[0] != '-') ||
      tzName[3] != ':') {
    return std::nullopt;
  }
  for (const std::size_t i : {1U, 2U, 4U, 5U}) {
    if (tzName[i] < '0' || tzName[i] > '9') {
      return std::nullopt;
    }
  }
  const int hours   = (tzName[1] - '0') * 10 + (tzName[2] - '0');
  const int minutes = (tzName[4] - '0') * 10 + (tzName[5] - '0');
  const std::chrono::minutes offset{hours * 60 + minutes};
  return tzName[0] == '-' ? -offset : offset;
}

} // namespace

std::chrono::sys_time<std::chrono::nanoseconds>
localTimeToUtc(const std::string& tzName,
               std::chrono::local_time<std::chrono::nanoseconds> localTime) {
  if (const std::optional<std::chrono::minutes> offset =
          parseUtcOffset(tzName)) {
    return std::chrono::sys_time<std::chrono::nanoseconds>(
               localTime.time_since_epoch()) -
           *offset;
  }
  const tz::time_zone* zone = getTimezone(tzName);
  // Wall-clock times repeated or skipped by a DST change map to the earlier
  // instant instead of throwing.
#if defined(TRINO_ODBC_USE_DATE_TZ)
  // date::local_time uses its own clock tag; the duration is the same.
  date::local_time<std::chrono::nanoseconds> dateLocal(
      localTime.time_since_epoch());
  return tz::zoned_time<std::chrono::nanoseconds>(
             zone, dateLocal, date::choose::earliest)
      .get_sys_time();
#else
  return tz::zoned_time<std::chrono::nanoseconds>(
             zone, localTime, std::chrono::choose::earliest)
      .get_sys_time();
#endif
}
