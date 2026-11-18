#include "../../util/browserInteraction.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#include "../../util/writeLog.hpp"

void openURLInDefaultBrowser(const std::string& url) {
  if (!isHttpUrl(url)) {
    WriteLog(LL_ERROR,
             "  ERROR: Refusing to open a non-http(s) authentication URL");
    return;
  }

  // LaunchServices works from inside the Excel sandbox, unlike spawning
  // /usr/bin/open.
  CFURLRef cfUrl =
      CFURLCreateWithBytes(kCFAllocatorDefault,
                           reinterpret_cast<const UInt8*>(url.data()),
                           static_cast<CFIndex>(url.size()),
                           kCFStringEncodingUTF8,
                           nullptr);
  if (cfUrl == nullptr) {
    WriteLog(LL_ERROR, "  ERROR: Could not parse the authentication URL");
    return;
  }

  OSStatus status = LSOpenCFURLRef(cfUrl, nullptr);
  CFRelease(cfUrl);

  if (status != noErr) {
    WriteLog(LL_ERROR,
             "  ERROR: Failed to open default browser, OSStatus " +
                 std::to_string(status));
  }
}
