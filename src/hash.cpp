#include "hash.h"
#include <functional>

size_t Hash::HashString(const std::string &str) noexcept {
  static thread_local std::hash<std::string> hasher;
  return hasher(str);
}
size_t Hash::HashString(std::string_view str) noexcept {
  static thread_local std::hash<std::string_view> hasher;
  return hasher(str);
}
