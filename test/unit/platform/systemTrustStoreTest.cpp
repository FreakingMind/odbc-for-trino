#include "gtest/gtest.h"

#include <cstddef>
#include <string>

#include "../../../src/platform/systemTrustStore.hpp"

#ifdef __APPLE__
namespace {

std::size_t countOccurrences(const std::string& text,
                             const std::string& needle) {
  std::size_t count = 0;
  for (std::size_t pos = text.find(needle); pos != std::string::npos;
       pos             = text.find(needle, pos + needle.size())) {
    count++;
  }
  return count;
}

} // namespace
#endif

TEST(SystemTrustStoreTest, ComputedOnce) {
  EXPECT_EQ(&platform::systemTrustStore(), &platform::systemTrustStore());
}

#ifdef __APPLE__
TEST(SystemTrustStoreTest, ExportsTheSystemRootsAsPem) {
  const platform::SystemTrustStore& store = platform::systemTrustStore();
  // macOS ships well over a hundred root certificates.
  EXPECT_GT(store.certificateCount, 50U);
  EXPECT_EQ(store.pem.rfind("-----BEGIN CERTIFICATE-----\n", 0), 0U);
  EXPECT_EQ(countOccurrences(store.pem, "-----BEGIN CERTIFICATE-----"),
            store.certificateCount);
  EXPECT_EQ(countOccurrences(store.pem, "-----END CERTIFICATE-----\n"),
            store.certificateCount);
}
#else
TEST(SystemTrustStoreTest, NotNeededWithSchannel) {
  EXPECT_TRUE(platform::systemTrustStore().pem.empty());
}
#endif
