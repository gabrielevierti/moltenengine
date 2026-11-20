// GameMenu.cpp
// The game's screens: the title, the race, the pause, and the settings. Put it on any one
// entity. It works with interface pieces by their names:
//
//   "Title Screen"       a group shown first; its button "Race" starts, "Title Settings"
//                        opens the settings
//   "Interface"          the race's own interface: hidden on the title
//   "Pause Button"       pauses (as Escape and P do)
//   "Pause Screen"       a group: "Resume", "Restart", "Reset Car", "Pause Settings", "Main Menu"
//   "Settings Screen"    a group with "Settings Back", and the options: buttons named
//                        "Option <preference> <value>". A click keeps that value under that
//                        name, from one time the game is played to the next; the button of
//                        the value kept stands out.
//
// The preferences it applies itself: "quality" (0 low, 1 medium, 2 high: the lighting, the
// shadows and how sharp the picture is), "volume" (0 off, 1 half, 2 full), "controls"
// (the size of the touch buttons: 0 small, 1 medium, 2 large). The others ("weather",
// "abs", "startCompound") are read by the scripts they belong to.
#include "MoltenMetal.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

using namespace molten;

struct GameMenu {
    bool showsTitle = true;             // off: straight into the race

    enum class Screen { Title, Race, Paused, Settings };
    Screen screen = Screen::Title;
    Screen settingsFrom = Screen::Title;
    std::map<std::string, float> touchRadius;   // each touch button as it was made
    int appliedQuality = -1;
    float appliedControls = -1;
    // Starting over from the pause skips the title: the scene comes back without it.
    static inline bool skipsTitleOnce = false;

    static void show(World& world, const char* name, bool shown) {
        if (Transform* group = world.scene.get<Transform>(world.find(name))) group->isActive = shown;
    }
    static bool clicked(World& world, const char* name) {
        const kit::UIButton* button = world.scene.get<kit::UIButton>(world.find(name));
        return button && button->wasClicked;
    }

    void go(World& world, Screen next) {
        if (next == Screen::Settings && screen != Screen::Settings) settingsFrom = screen;
        screen = next;
        show(world, "Title Screen", screen == Screen::Title);
        show(world, "Pause Screen", screen == Screen::Paused);
        show(world, "Settings Screen", screen == Screen::Settings);
        // The race's interface is not on the title, nor on settings opened from the title.
        bool racing = screen == Screen::Race || screen == Screen::Paused || (screen == Screen::Settings && settingsFrom != Screen::Title);
        show(world, "Interface", racing);
        // Everything stands still but the race; the interface still answers.
        world.time.timeScale = screen == Screen::Race ? 1.0f : 0.0f;
        world.scene.updateTransforms();
    }

    void applyQuality(World& world, int quality) {
        RenderSettings& settings = world.sceneSettings();
        // Low: shadow maps alone, a softer picture; Medium: traced sun shadows; High: traced light.
        applyLightingQuality(settings, quality <= 0 ? LightingQuality::Low : quality == 1 ? LightingQuality::Medium : LightingQuality::High);
        settings.renderScale = quality <= 0 ? 0.7f : quality == 1 ? 0.85f : 1.0f;
        settings.ambientOcclusionEnabled = quality >= 1;
        settings.bloomEnabled = quality >= 1;
        appliedQuality = quality;
    }

    void start(World& world, Entity) {
        // Each touch button's size as it was made: the setting scales from there.
        world.scene.each<kit::UIButton, Name>([&](Entity, kit::UIButton& button, Name& name) {
            if (button.isRound && button.onlyOnTouchScreens) touchRadius[name.value] = button.radius;
        });
        go(world, showsTitle && !skipsTitleOnce ? Screen::Title : Screen::Race);
        skipsTitleOnce = false;
    }

    void update(World& world, Entity, float) {
        // The settings, applied when they change.
        int quality = std::clamp(int(world.preference("quality", 1)), 0, 2);
        if (quality != appliedQuality) applyQuality(world, quality);
        float controls = std::clamp(world.preference("controls", 1), 0.0f, 2.0f);
        if (controls != appliedControls) {
            float scale = controls < 0.5f ? 0.8f : controls < 1.5f ? 1.0f : 1.25f;
            world.scene.each<kit::UIButton, Name>([&](Entity, kit::UIButton& button, Name& name) {
                if (auto made = touchRadius.find(name.value); made != touchRadius.end()) button.radius = made->second * scale;
            });
            appliedControls = controls;
        }
        float volume = std::clamp(world.preference("volume", 2), 0.0f, 2.0f) * 0.5f;
        world.soundVolume = screen == Screen::Race ? volume : volume * 0.25f;

        bool pausePressed = world.input.wasPressed(Key::Escape) || world.input.wasPressed(Key::P);
        switch (screen) {
            case Screen::Title:
                if (clicked(world, "Race") || world.input.wasPressed(Key::Enter) || world.input.wasPressed(Key::Space)) go(world, Screen::Race);
                else if (clicked(world, "Title Settings")) go(world, Screen::Settings);
                break;
            case Screen::Race:
                if (clicked(world, "Pause Button") || pausePressed) go(world, Screen::Paused);
                break;
            case Screen::Paused:
                if (clicked(world, "Resume") || pausePressed) go(world, Screen::Race);
                else if (clicked(world, "Pause Settings")) go(world, Screen::Settings);
                else if (clicked(world, "Reset Car")) {
                    world.setValue("resetCar", 1);
                    go(world, Screen::Race);
                } else if (clicked(world, "Restart")) {
                    skipsTitleOnce = true;
                    world.time.timeScale = 1.0f;
                    world.reloadScene();
                } else if (clicked(world, "Main Menu")) {
                    world.time.timeScale = 1.0f;
                    world.reloadScene();
                }
                break;
            case Screen::Settings:
                if (clicked(world, "Settings Back") || world.input.wasPressed(Key::Escape)) go(world, settingsFrom);
                break;
        }

        // The options: "Option weather 2" keeps 2 as "weather"; the one kept stands out.
        world.scene.each<kit::UIButton, Name>([&](Entity, kit::UIButton& button, Name& name) {
            if (name.value.compare(0, 7, "Option ") != 0) return;
            size_t space = name.value.rfind(' ');
            if (space <= 7) return;
            std::string key = name.value.substr(7, space - 7);
            float value = std::strtof(name.value.c_str() + space + 1, nullptr);
            if (button.wasClicked) world.setPreference(key, value);
            bool chosen = std::abs(world.preference(key, defaultOf(key)) - value) < 0.01f;
            button.color.w = chosen ? 0.95f : 0.3f;
            button.textColor.w = chosen ? 1.0f : 0.75f;
        });
    }

    /// What each setting is until it is chosen.
    static float defaultOf(const std::string& key) {
        if (key == "quality" || key == "controls" || key == "startCompound" || key == "abs") return 1;
        if (key == "volume") return 2;
        return 0;
    }
};

MOLTEN_COMPONENT(GameMenu, MOLTEN_FIELD(showsTitle))
