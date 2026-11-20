Vanta v0.1.0 preview

Download **Vanta-v0.1.0-win64-with-source.zip** and extract the entire archive. Start Pixel Gun 3D, open Vanta.exe, then click Load Vanta. Press INSERT in-game to open the menu.

Included:
- Windows x64 launcher with one-click loading and duplicate-load checks.
- Compiled DLL, six model assets and matching source in source/.
- Refreshed menu with Overview, clear navigation and Farm progress.
- Stored match-kill counting, assistance cutoff, reward/replay controls.
- UI-update dispatch, retained button references and a re-entry guard for the respawn crash investigation.
- Automatic-click guard for foreground window, cursor position and Alt/Windows modifiers.
- SHA-256 checksums and dependency license notices.

Validation: Release build, Farm regression tests, model-handle regression tests, six-model format validation, ZIP integrity and source/binary identity checks. The launcher and DLL use a static C++ runtime.

Known limits: preview build; long-session stability and the focus fix still need live verification. Custom models are experimental static meshes, with in-game shader/display issues still under investigation. Farm pauses when another app has focus. Personal settings, logs and crash dumps are excluded from this package.
