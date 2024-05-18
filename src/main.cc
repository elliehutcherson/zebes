#include <iostream>

#include "engine/game.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"


int main(int argc, char* argv[]) {
    absl::ParseCommandLine(argc, argv);
    if (argc < 2) {
        std::cout << "ECLI did not receive any command..." << std::endl;
        return 0;
    }

    std::string program = argv[1];
    if (program == "zebes") {
        zebes::GameConfig config = zebes::GameConfig::Create();
        zebes::Game game(config);
        absl::Status result = game.Init();
        if (result.ok()) result = game.Run();
        std::cout << "exiting zebes: " << result.ToString() << std::endl;
    } else {
        std::cout << "ECLI did not receive a recognized command..." << std::endl;
    }
    
    return 0;
}