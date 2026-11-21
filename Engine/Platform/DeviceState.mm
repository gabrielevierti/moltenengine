// DeviceState.mm: the system's own readings, from Foundation's ProcessInfo.
#include "Platform/DeviceState.hpp"

#import <Foundation/Foundation.h>

namespace molten {

ThermalState thermalState() {
    switch (NSProcessInfo.processInfo.thermalState) {
        case NSProcessInfoThermalStateNominal: return ThermalState::Nominal;
        case NSProcessInfoThermalStateFair: return ThermalState::Fair;
        case NSProcessInfoThermalStateSerious: return ThermalState::Serious;
        case NSProcessInfoThermalStateCritical: return ThermalState::Critical;
    }
    return ThermalState::Nominal;
}

bool isLowPowerModeOn() {
    return NSProcessInfo.processInfo.isLowPowerModeEnabled;
}

std::string preferredLanguage() {
    NSString* first = NSLocale.preferredLanguages.firstObject;        // "it-IT", "en-GB"
    if (!first) return "";
    std::string code = first.UTF8String;
    size_t dash = code.find('-');
    return dash == std::string::npos ? code : code.substr(0, dash);
}

}  // namespace molten
