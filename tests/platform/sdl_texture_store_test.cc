#include "platform/sdl/sdl_texture_store.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "platform/sdl/sdl_texture_handle.h"

namespace zebes {
namespace {

class FakeSdlWrapper : public SdlWrapper {
 public:
  FakeSdlWrapper() : SdlWrapper(nullptr, nullptr) {}

  absl::StatusOr<SDL_Texture*> CreateTexture(const std::string& path) override {
    loaded_paths.push_back(path);
    return reinterpret_cast<SDL_Texture*>(next_native_value_++);
  }

  void DestroyTexture(SDL_Texture* texture) override { destroyed.push_back(texture); }

  std::vector<std::string> loaded_paths;
  std::vector<SDL_Texture*> destroyed;

 private:
  uintptr_t next_native_value_ = 0x1000;
};

TEST(SdlTextureStoreTest, UsesStableHandlesAndResolvesNativeTextures) {
  FakeSdlWrapper sdl;
  SdlTextureStore store(sdl);

  ASSERT_TRUE(store.Load("first.png").ok());
  absl::StatusOr<TextureHandle> second = store.Load("second.png");
  ASSERT_TRUE(second.ok());

  EXPECT_EQ(second->id(), 2);
  EXPECT_EQ(SdlTextureHandleAdapter::ToNative(*second),
            reinterpret_cast<SDL_Texture*>(0x1001));
  EXPECT_EQ(sdl.loaded_paths, (std::vector<std::string>{"first.png", "second.png"}));
}

TEST(SdlTextureStoreTest, UnloadDestroysAndInvalidatesResource) {
  FakeSdlWrapper sdl;
  SdlTextureStore store(sdl);
  absl::StatusOr<TextureHandle> handle = store.Load("texture.png");
  ASSERT_TRUE(handle.ok());

  EXPECT_TRUE(store.Unload(*handle).ok());
  ASSERT_EQ(sdl.destroyed.size(), 1);
  EXPECT_EQ(SdlTextureHandleAdapter::ToNative(*handle), nullptr);
  EXPECT_FALSE(store.Unload(*handle).ok());
}

}  // namespace
}  // namespace zebes
