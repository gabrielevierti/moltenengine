// FPV, a drone racing game made with Molten.
//
//   fpv                    the menu
//   fpv --race             straight into a race        --free   straight into free flight
//   fpv --whoop            the small drone             --acro   rates instead of self-leveling
//   fpv --chase            seen from behind            --sticks draw the stick positions
#include "FPVGame.hpp"

int main(int argc, char** argv) {
    molten::EngineConfig defaults;
    defaults.title = "FPV";
    molten::Engine engine(molten::EngineConfig::fromArguments(argc, argv, defaults));
    FPVGame game;
    return engine.run(game);
}
