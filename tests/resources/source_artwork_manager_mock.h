#pragma once

#include <gmock/gmock.h>

#include <string>
#include <vector>

#include "resources/source_artwork_manager.h"

namespace zebes {

class SourceArtworkManagerMock : public SourceArtworkManager {
 public:
  MOCK_METHOD(absl::Status, LoadAllArtwork, (), (override));
  MOCK_METHOD(absl::StatusOr<std::string>, CreateArtwork,
              (std::string, SourceArtworkProvenance, const RgbaImage&), (override));
  MOCK_METHOD(absl::Status, ReplaceArtwork,
              (const SourceArtwork&, const SourceArtwork&, const RgbaImage&), (override));
  MOCK_METHOD(absl::StatusOr<SourceArtwork*>, GetArtwork, (const std::string&), (override));
  MOCK_METHOD(std::vector<SourceArtwork>, GetAllArtwork, (), (const, override));
  MOCK_METHOD(absl::StatusOr<RgbaImage>, ReadArtworkPixels, (const std::string&),
              (const, override));
  MOCK_METHOD(absl::Status, DeleteArtwork, (const std::string&), (override));
};

}  // namespace zebes
