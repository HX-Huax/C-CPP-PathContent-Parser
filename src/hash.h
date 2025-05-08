#ifndef __HAS_HASH__
#define __HAS_HASH__
#include <string>
#include <string_view>
class Hash {
public:
  static size_t HashString(const std::string &str) noexcept;
  static size_t HashString(std::string_view str) noexcept;
};

#endif // !DEBUG
