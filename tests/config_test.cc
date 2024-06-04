#include "engine/config.h"

#include "nlohmann/json.hpp"

#include "absl/log/log.h"

int main() {
  zebes::GameConfig config = zebes::GameConfig::Create();
  nlohmann::json json_data = config;
  LOG(INFO) << json_data.dump(2);

  return 0;
}
