#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/tileset_manager.h"

namespace zebes {

class TilesetManagerMock : public TilesetManager {
 public:
  TilesetManagerMock() : TilesetManager("") {}

  MOCK_METHOD(absl::StatusOr<Tileset*>, LoadTileset, (const std::string& path_json), (override));
  MOCK_METHOD(absl::Status, LoadAllTilesets, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateTileset, (Tileset tileset), (override));
  MOCK_METHOD(absl::Status, SaveTileset, (const Tileset& tileset), (override));
  MOCK_METHOD(absl::StatusOr<Tileset*>, GetTileset, (const std::string& id), (override));
  MOCK_METHOD(absl::Status, DeleteTileset, (const std::string& id), (override));
  MOCK_METHOD(std::vector<Tileset>, GetAllTilesets, (), (const, override));
  MOCK_METHOD(bool, IsTextureUsed, (const std::string& texture_id), (const, override));
};

}  // namespace zebes
