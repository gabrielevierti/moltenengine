# iPhone and iPad

The engine runs on iOS and iPadOS 16 and later. What follows is how to put a project on a device today, how it works, and what is still missing.

## Putting a project on a device

You need Xcode, an Apple developer account (a free one is enough for your own device), and the project you made in the editor.

```sh
cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DMOLTEN_IOS_PROJECT=$HOME/Games/MyGame \
      -DMOLTEN_IOS_BUNDLE_ID=com.yourname.mygame \
      -DMOLTEN_IOS_NAME="My Game" \
      -DMOLTEN_IOS_TEAM=ABCDE12345
open build-ios/MoltenMetalEngine.xcodeproj
```

In Xcode choose the `MoltenPlayer` scheme and your device, and press Run.

| Option | |
|---|---|
| `MOLTEN_IOS_PROJECT` | The project's folder. Without it the application shows the engine's sample scene: a quick way to see that everything works. |
| `MOLTEN_IOS_BUNDLE_ID` | The application's identifier. It must be yours and unique. |
| `MOLTEN_IOS_NAME` | The name under the icon. |
| `MOLTEN_IOS_TEAM` | Your team identifier (Xcode > Settings > Accounts, or developer.apple.com > Membership). It can also be set in Xcode, under Signing & Capabilities. |

For the simulator add `-DCMAKE_OSX_SYSROOT=iphonesimulator`. Ray tracing does not work there; the engine notices and uses shadow maps.

**After changing assets or scenes, run the `cmake` line again**: the project's `Assets` folder is copied when CMake runs. Changes to the code in `Source` only need Run.

## How it works

- **The application is the player** (`Player/Main.cpp`), the same program an exported Mac game is. It loads the project packed beside it and plays its first scene.
- **The game's code is compiled in.** On a Mac the player loads the project's code as a library; iOS does not allow an application to load code it did not ship as part of itself, so the project's `Source/*.cpp` are compiled into the player. `MOLTEN_COMPONENT` works the same either way.
- **The platform layer** is one file, `Engine/Platform/iOS/PlatformiOS.mm`: a `UIView` whose layer is a `CAMetalLayer`, a scene delegate that starts the engine when the scene connects, and a `CADisplayLink` that calls `Engine::frame()` once per display refresh (60 times per second, up to 120 on ProMotion). Rotation, split view and any other change of size reach the engine as a new drawable size.
- **Touch.** `engine.input.touches()` lists the fingers on the screen. The first finger also acts as the mouse with its left button down, so interfaces written for a mouse work under a thumb.
- **Shaders** are compiled on the device when the application starts, from the same `.metal` files as on the Mac.
- **Ray tracing** works on every device from the A13 on.

## What is missing

- Game controllers and the on-screen keyboard.
- Safe areas reported to the game (the notch, the home indicator): the view covers the whole screen and the game must keep clear of the edges itself.
- An icon and a launch image for the application.
- Building for iOS from the editor's Game menu; packing assets (they are copied as they are); App Store packaging.
- Pausing the simulation's clock in the background: the game stops drawing there, and resumes with one long frame.
