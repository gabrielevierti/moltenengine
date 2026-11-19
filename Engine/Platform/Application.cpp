// The Mac's version of runApplication(). The iOS one is in Platform/iOS/ApplicationiOS.mm.
#include "Platform/Application.hpp"

namespace molten {

int runApplication(int argc, char** argv, EngineConfig defaults, GameFactory makeGame) {
    Engine engine(EngineConfig::fromArguments(argc, argv, std::move(defaults)));
    std::unique_ptr<Game> game = makeGame(engine);
    if (!game) return 1;
    return engine.run(*game);
}

}  // namespace molten
