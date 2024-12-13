#include <string>

#include "absl/status/statusor.h"

#include "engine/config.h"

namespace zebes {

class CreatorApi {
 public: 
  struct Options {
    std::string import_path;
  };

  static absl::StatusOr<CreatorApi> Create(const Options& options);

  absl::Status SaveConfig(const GameConfig& config);
  
  absl::StatusOr<GameConfig> ImportConfig();

  private:
    CreatorApi(const Options& options);

    const std::string import_path_;
};

} // namespace zebes
