#include "jsonValueToText.hpp"

std::string jsonValueToText(const nlohmann::json& value) {
  switch (value.type()) {
    case nlohmann::json::value_t::string:
      return value.get<std::string>();
    case nlohmann::json::value_t::boolean:
      return value.get<bool>() ? "1" : "0";
    case nlohmann::json::value_t::number_integer:
      return std::to_string(value.get<int64_t>());
    case nlohmann::json::value_t::number_unsigned:
      return std::to_string(value.get<uint64_t>());
    case nlohmann::json::value_t::null:
      return "";
    default:
      // Floats (nlohmann prints the shortest round-trip form), arrays and
      // objects.
      return value.dump();
  }
}
