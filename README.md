# Vanta

A Windows x64 client for Pixel Gun 3D PC Edition, with a portable launcher, in-game controls and Deathmatch automation.

**v0.1.11 preview** ? active development. Game updates can break compatibility. Custom models remain experimental; their files are static meshes without skeletal animation.

## Download and run

Download `Vanta-v0.1.11-win64-with-source.zip` from Releases and **extract the entire ZIP** into a writable folder.

1. Start Pixel Gun 3D and wait for the main menu.
2. Open `Vanta.exe` and click **Load Vanta**.
3. Return to the game. Press **INSERT** to open the controls.

Keep `pg3d_core.dll`, `models/` and `offsets.json` beside `Vanta.exe`. No Python, Visual Studio or command-line arguments are needed to run the package. The launcher checks for an already-loaded DLL to avoid duplicate loads.

| Key | Action |
| --- | --- |
| INSERT | Open or close the menu |
| DELETE | Toggle panic: hide overlays and pause assistance |
| END | Unload the DLL |

The menu and panic keys can be changed in Settings. Closing the launcher does not unload the DLL; use END in-game.

## Controls

In v0.1.11, automatic firing uses the earlier Windows mouse press/release implementation. The experimental Unity input replacement and cursor-location gate remain rolled back. Gun notices with a collider-only background are now recognized by their explicit ?Press anywhere to close? prompt, even when the same panel also says ?Available for purchase?. Farm retains Deathmatch/Nuclear City navigation, XP claims, new-weapon dismissal, reward priority and delayed replay.

- **Overview:** session status, shortcuts and keyboard controls.
- **Aim & fire:** aim assistance, silent aim, target cone, automatic firing, and experimental rapid fire. Rapid fire has a saved 1x?50x requested speed multiplier (2x defaults to half the normal delay). It hooks a verified effective cooldown getter for the local weapon, including calls outside Update; it is paused during Farm. Its call/reduction counters and returned delay are logged every three seconds while enabled. Actual firing/damage rates require in-game verification and may be limited by the server.
- **Triggerbot:** fire when a target crosses the selected gate.
- **Visuals:** player boxes, labels, radar and colors.
- **Camera & models:** FOV, third person and experimental static models.
- **Sounds:** 48 bundled WAV files in `sounds/`. You can also import your own 16-bit PCM WAV files.
- **Farm:** optional wallbang toggle (no hold key during Farm combat), saved with your presets. Configure a kill limit, then press Start farm and close the menu. It reads stored match kills, disables gameplay assistance at the limit, and handles recognized respawn, trophy, reward and replay controls. Zero kills selects an idle run. Stop farm restores your previous settings.
- **Config:** save named presets, overwrite them, or load a selected preset from `configs/`. Stop Farm before loading.
- **Debug / Settings:** diagnostics, keybinds and settings persistence.

Farm and automatic firing require the game to stay focused. The rollback has been compiled and regression-tested; this build has not been tested live in-game.

## Build from source

Requirements: Windows x64, Visual Studio 2022 with Desktop development with C++, Git and CMake 3.20+.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target Vanta pg3d_core pg3d_farm_test pg3d_model_handle_test
ctest --test-dir build -C Release --output-on-failure
powershell -ExecutionPolicy Bypass -File tools/package.ps1
```

CMake fetches pinned versions of Dear ImGui, MinHook and nlohmann/json. Dependency license notices are in `third_party/`. The launcher and DLL use the static C++ runtime.

The release ZIP contains a `source/` folder with the source used for the build. GitHub's automatically generated source archives do not contain compiled binaries.

## Troubleshooting and limits

- Extract the ZIP before running it. Launching from a ZIP viewer may leave the DLL or assets missing.
- Start the game first. If Windows reports access denied, check whether the two programs use different privilege levels.
- Settings are in `config.json`; diagnostics are in `pg3d_core.log`, both beside the DLL. These personal files are excluded from source and release packages.
- Farm does not run while you use another app. Unknown reward controls can require manual input.
- A crash occurred after a respawn in the development build. UI actions were moved out of the rendering callback, object references were retained, and re-entry was blocked. Compilation and logic tests pass; this does not establish long-session stability.
- The focus guard and redesigned menu are compiled but still need a live gameplay check.
- Custom-model assets pass offline format validation. Correct appearance across game shaders is not yet verified.
- This project is not affiliated with the game's developer. No detection or account-safety guarantees are made.

## Source and assets

Source is published for inspection and building. No license for the original project code is granted by this release. Dependency notices retain their own terms. Bundled model files were supplied with this project; no authorship or license rights to those character assets are claimed here.

## Updating

Click **Update Vanta** in the launcher after unloading the DLL with END. The hidden PowerShell helper checks public GitHub releases (including previews), verifies the download checksum, updates the same installation folder, and reopens the launcher. Existing `config.json`, `configs/`, and sound files are preserved. One sibling `Vanta.zip` is refreshed. A failed install restores replaced files. Network errors leave the installed build available.
