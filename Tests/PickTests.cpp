// Text fields that name something are chosen from a list in the editor, never typed: these
// tests keep every new component honest about which kind of thing its text names.
#include "Test.hpp"

#include "Game/Component.hpp"

#include <set>
#include <string>

using namespace molten;

TEST(every_text_field_says_what_it_names) {
    // The fields that are words people read (or a name the field itself gives): free text.
    std::set<std::string> words = {"UIButton.label", "UIGauge.label", "UISlider.label", "UIToggle.label", "UITextField.placeholder", "TouchButton.label",
                                   "HudText.text", "Goal.message", "Saveable.id", "TestSpinner.label", "TestLife.label", "RaceDirector.drivers",
                                   // An address, a name, and lists of names by commas: typed.
                                   "NetworkSession.address", "NetworkSession.playerName", "NetworkSession.sharedValues", "Networked.ownerOnly", "Networked.forwards"};
    std::string free;
    for (const ComponentType& type : ComponentRegistry::instance().types()) {
        for (const Field& field : type.fields) {
            if (field.type != FieldType::String || !field.options.empty() || !field.pick.empty() || !field.pickBy.empty() || field.isHidden) continue;
            std::string id = type.name + "." + field.name;
            if (!words.count(id)) free += id + "\n";
        }
    }
    std::printf("%s", free.c_str());
    CHECK(free.empty());
}
