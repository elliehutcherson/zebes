#pragma once

#include <cstdint>

namespace zebes {

struct TextureHandleAccess;

// Opaque renderer resource identifier. Engine and resource data may copy and
// compare this value without depending on a rendering backend's types.
class TextureHandle {
 public:
  constexpr TextureHandle() = default;

  constexpr bool IsValid() const { return value_ != 0; }
  constexpr explicit operator bool() const { return IsValid(); }
  constexpr uint64_t id() const { return value_; }

  friend constexpr bool operator==(TextureHandle, TextureHandle) = default;

 private:
  explicit constexpr TextureHandle(uint64_t value, const void* owner)
      : value_(value), owner_(owner) {}

  uint64_t value_ = 0;
  const void* owner_ = nullptr;

  friend struct TextureHandleAccess;
};

// Internal bridge used by resource-store implementations. Application code
// should treat TextureHandle as an opaque value.
struct TextureHandleAccess {
  static constexpr TextureHandle Create(uint64_t id, const void* owner) {
    return TextureHandle(id, owner);
  }

  static constexpr const void* Owner(TextureHandle handle) { return handle.owner_; }
};

}  // namespace zebes
