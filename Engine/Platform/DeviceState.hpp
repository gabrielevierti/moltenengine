// DeviceState.hpp
// How the machine is doing: how hot it is, and whether it is saving its battery. A game
// lowers its quality when the device heats up, before the system slows it down by itself.
#pragma once

#include <string>

namespace molten {

enum class ThermalState {
    Nominal,        // all is well
    Fair,           // warm: a little less would be wise
    Serious,        // hot: the system is about to slow things down; draw less now
    Critical,       // very hot: draw as little as possible
};

/// The system's reading, now (ProcessInfo.thermalState).
ThermalState thermalState();
/// Low Power Mode is on (iPhone and iPad, and Macs with it in their battery settings).
bool isLowPowerModeOn();
/// The language the person reads, as the system's settings say: "it", "en", "fr".
std::string preferredLanguage();

}  // namespace molten
