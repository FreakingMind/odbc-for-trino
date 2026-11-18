#include "gtest/gtest.h"
#include <chrono>
#include <stdexcept>
#include <string>

#include "../../../src/util/timezone.hpp"

namespace {

using namespace std::chrono;

local_time<nanoseconds>
localAt(int y, unsigned m, unsigned d, hours h, minutes min = minutes{0}) {
  local_days date{year{y} / month{m} / std::chrono::day{d}};
  return local_time<nanoseconds>(date.time_since_epoch() + h + min);
}

sys_time<nanoseconds>
utcAt(int y, unsigned m, unsigned d, hours h, minutes min = minutes{0}) {
  sys_days date{year{y} / month{m} / std::chrono::day{d}};
  return sys_time<nanoseconds>(date.time_since_epoch() + h + min);
}

} // namespace

TEST(TimezoneTest, UtcIsIdentity) {
  EXPECT_EQ(localTimeToUtc("Etc/UTC", localAt(2024, 1, 15, hours{12})),
            utcAt(2024, 1, 15, hours{12}));
  EXPECT_EQ(localTimeToUtc("Etc/UTC", localAt(2024, 7, 15, hours{0})),
            utcAt(2024, 7, 15, hours{0}));
}

TEST(TimezoneTest, MoscowIsPlusThreeWithoutDst) {
  // Moscow has been UTC+3 all year round since October 2014.
  EXPECT_EQ(localTimeToUtc("Europe/Moscow", localAt(2024, 1, 15, hours{12})),
            utcAt(2024, 1, 15, hours{9}));
  EXPECT_EQ(localTimeToUtc("Europe/Moscow", localAt(2024, 7, 15, hours{12})),
            utcAt(2024, 7, 15, hours{9}));
  // Crossing midnight backwards.
  EXPECT_EQ(localTimeToUtc("Europe/Moscow", localAt(2024, 3, 1, hours{1})),
            utcAt(2024, 2, 29, hours{22}));
}

TEST(TimezoneTest, NewYorkHonoursDst) {
  // Winter: EST, UTC-5.
  EXPECT_EQ(localTimeToUtc("America/New_York", localAt(2024, 1, 15, hours{12})),
            utcAt(2024, 1, 15, hours{17}));
  // Summer: EDT, UTC-4.
  EXPECT_EQ(localTimeToUtc("America/New_York", localAt(2024, 7, 15, hours{12})),
            utcAt(2024, 7, 15, hours{16}));
  // Either side of the 2024-03-10 02:00 spring-forward transition.
  EXPECT_EQ(localTimeToUtc("America/New_York", localAt(2024, 3, 10, hours{1})),
            utcAt(2024, 3, 10, hours{6}));
  EXPECT_EQ(localTimeToUtc("America/New_York", localAt(2024, 3, 10, hours{3})),
            utcAt(2024, 3, 10, hours{7}));
}

TEST(TimezoneTest, KolkataHalfHourOffset) {
  EXPECT_EQ(localTimeToUtc("Asia/Kolkata", localAt(2024, 1, 15, hours{12})),
            utcAt(2024, 1, 15, hours{6}, minutes{30}));
  EXPECT_EQ(localTimeToUtc("Asia/Kolkata", localAt(2024, 1, 15, hours{3})),
            utcAt(2024, 1, 14, hours{21}, minutes{30}));
}

TEST(TimezoneTest, UnknownZoneThrows) {
  EXPECT_THROW(localTimeToUtc("Not/A_Zone", localAt(2024, 1, 15, hours{12})),
               std::runtime_error);
  EXPECT_THROW(localTimeToUtc("", localAt(2024, 1, 15, hours{12})),
               std::runtime_error);
  // A failed lookup must not poison the cache for later valid lookups.
  EXPECT_THROW(localTimeToUtc("Not/A_Zone", localAt(2024, 1, 15, hours{12})),
               std::runtime_error);
  EXPECT_EQ(localTimeToUtc("Etc/UTC", localAt(2024, 1, 15, hours{12})),
            utcAt(2024, 1, 15, hours{12}));
}

TEST(TimezoneTest, NanosecondPrecisionPreserved) {
  const nanoseconds frac{123456789};
  auto result =
      localTimeToUtc("Europe/Moscow", localAt(2024, 1, 15, hours{12}) + frac);
  EXPECT_EQ(result, utcAt(2024, 1, 15, hours{9}) + frac);
  EXPECT_EQ((result.time_since_epoch() % seconds{1}).count(), 123456789);

  auto one = localTimeToUtc("Asia/Kolkata",
                            localAt(2024, 1, 15, hours{12}) + nanoseconds{1});
  EXPECT_EQ(one, utcAt(2024, 1, 15, hours{6}, minutes{30}) + nanoseconds{1});
}

TEST(TimezoneTest, RepeatedLookupsAreConsistent) {
  // The second call is served from the zone cache.
  auto first  = localTimeToUtc("Asia/Kolkata", localAt(2024, 5, 1, hours{8}));
  auto second = localTimeToUtc("Asia/Kolkata", localAt(2024, 5, 1, hours{8}));
  EXPECT_EQ(first, second);
}
