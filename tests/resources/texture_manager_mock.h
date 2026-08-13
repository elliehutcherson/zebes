#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/texture_manager.h"

namespace zebes {

class TextureManagerMock : public TextureManager {
 public:
  TextureManagerMock() : TextureManager(nullptr, "") {}

  MOCK_METHOD(absl::StatusOr<Texture*>, LoadTexture, (const std::string& path_json), (override));
  MOCK_METHOD(absl::Status, LoadAllTextures, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTexture, (Texture texture), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTextureFromPixels,
              (const std::string& name, int width, int height, absl::Span<const uint8_t> pixels),
              (override));
  MOCK_METHOD(absl::Status, ReplaceTexturePixels,
              (const std::string& id, int width, int height, absl::Span<const uint8_t> pixels),
              (override));
  MOCK_METHOD(absl::StatusOr<Texture*>, GetTexture, (const std::string& id), (override));
  MOCK_METHOD(absl::StatusOr<TextureHandle>, GetTextureHandle, (const std::string& id),
              (const, override));
  MOCK_METHOD(absl::Status, DeleteTexture, (const std::string& id), (override));
  MOCK_METHOD(std::vector<Texture>, GetAllTextures, (), (const, override));
  MOCK_METHOD(absl::Status, UpdateTexture, (const Texture& texture), (override));
};

}  // namespace zebes
