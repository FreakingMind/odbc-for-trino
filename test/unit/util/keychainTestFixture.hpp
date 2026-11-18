#pragma once

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

/*
Fixture for tests that touch the OS secret store.

On macOS the secret store is the login keychain. Keychain items are bound to
the code signature of the program that created them, and unsigned test
binaries change on every rebuild, so these tests would trigger a consent
dialog per run (and hang in headless CI). They therefore only run when
TRINO_ODBC_TEST_KEYCHAIN=1 is set, and use a separate keychain service name
so they never create or read the real driver's items.

On Windows (DPAPI) the tests always run.
*/
class SecretStoreTest : public ::testing::Test {
  protected:
    void SetUp() override {
#ifdef __APPLE__
      const char* enabled = std::getenv("TRINO_ODBC_TEST_KEYCHAIN");
      if (enabled == nullptr || std::string(enabled) != "1") {
        GTEST_SKIP() << "Keychain tests are opt-in: set "
                        "TRINO_ODBC_TEST_KEYCHAIN=1";
      }
      // Must happen before the first keychain access in this process.
      setenv("TRINO_ODBC_KEYCHAIN_SERVICE", "TrinoODBC-test", 0);
#endif
    }
};
