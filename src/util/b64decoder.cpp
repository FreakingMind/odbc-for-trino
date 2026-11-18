#include "b64decoder.hpp"

#include <stdexcept>
#include <vector>

#include "base64.hpp"
#include "writeLog.hpp"

std::string fromBase64url(const std::string& input) {
  /*
  NOTE: base64url encoding is different from base64 encoding.
  This decoder accepts both alphabets and optional padding, which is
  what JWT payloads need.
  */
  try {
    std::vector<uint8_t> decoded = base64Decode(input);
    return std::string(decoded.begin(), decoded.end());
  } catch (const std::invalid_argument& e) {
    std::string errorMessage =
        "Error decoding from base64url: " + std::string(e.what());
    WriteLog(LL_ERROR, errorMessage);
    throw std::runtime_error(errorMessage);
  }
}
