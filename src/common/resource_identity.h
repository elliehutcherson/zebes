#pragma once

#include <string_view>

namespace zebes {

inline bool IsPathSafeResourceId(std::string_view id) {
  if (id.empty()) return false;
  for (const char character : id) {
    const bool safe = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '-';
    if (!safe) return false;
  }
  return true;
}

// Display names are allowed to contain spaces and UTF-8, but definitions use
// them as one filename component. Separators, dot traversal, and controls are
// therefore never valid resource names.
inline bool IsSafeResourceName(std::string_view name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (const unsigned char character : name) {
    if (character == '/' || character == '\\' || character <= 31 || character == 127) {
      return false;
    }
  }
  return true;
}

}  // namespace zebes
