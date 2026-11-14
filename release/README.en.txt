Ninja Gaiden Sigma Enhancements 0.6.22

The Afterimages tab shows the detected CPU and graphics adapters. The default Auto profile uses the adapter actually rendering the game. Discrete GPUs use Standard with full-resolution masks and the full trail. Integrated GPUs use Integrated Graphics with half-resolution masks and up to two afterimages per frame. Both materials keep their lighting and refraction. Choose either profile manually and select Apply Profile to switch during play. The setting is saved for the selected game folder.
Diagnostics record the active game adapter, effective profile, and frame submission intervals with and without afterimages.

Both profiles use the current renderer compatibility fixes. Integrated Graphics skips automatic scene depth sampling. Intel GPUs also keep the protective depth-clear bypass by default in Standard. This can affect wall occlusion.

Choose English or 简体中文 in the upper-right corner of the installer. Changes apply immediately, and the installer remembers your selection.
In windowed mode, rumble and afterimages follow gameplay when the pointer leaves the game window or another window gains focus.

Installation and restore
1. Exit the game. Extract the package, run NGS1RumblePatch.exe, and select NINJA GAIDEN SIGMA.exe.
2. Install or update Rumble, DualSense, or Afterimages. You can restore each feature separately. Existing 0.6.x installations can be updated in place.
3. Open the game EXE normally or use Launch Game. In compatibility mode, the per-user autoloader takes a few seconds to load the installed features.
4. Restore All removes the patch runtime and its startup entry. Your game files and saves remain in place.

Installation errors and game crashes
Reopen the installer, select the affected game EXE, and click Export Diagnostics. Review the ZIP before sharing it with the steps to reproduce the problem. It includes recent installer errors, game version and hashes, available loader and runtime logs, and Windows crash summaries for the selected game. Export replaces the game and Windows user directories with placeholders.
After a crash, choose Trace Game Crash to relaunch, reproduce the crash, and capture module addresses and available stack frames. Export Diagnostics includes the trace. For missing afterimages, export after trying the affected moves. The log records captured costume draws and sampled GPU coverage.

Afterimages
Choose Classic Material or the refractive sigma2 Material, then choose a costume. Each costume has separate glow strength, color, and transparency settings for each material. Glow strength on both materials and surface brightness on sigma2 go up to 200%, doubling the corresponding contribution at 100%. Update the afterimage runtime first to use the new range. Foreground geometry occludes afterimages when scene depth is available. Legendary Black Falcon, Legendary Ninja (Blue), and Grip of Murder also offer a muscle highlight control with a default of 45%.
Save and Apply Material applies the selected material to all costumes and reloads changes in a running game. Each costume uses its own saved settings. Ctrl+F11 previews the effect and Ctrl+F12 stops the preview.

Supported builds
The installer verifies known 1.0.0.2 and 1.0.0.4 executables, including the tested 1.0.0.4 Chinese localization by imjinjia. A matching version number alone does not establish compatibility. If verification fails, check the selected EXE and any patches that modify it.

Controller support
The DualSense feature includes automatic button icons. Use a DualSense to see the PlayStation face and shoulder button symbols. Switching back to Xbox restores its original icons. The patch draws the PlayStation symbols at runtime. DualSense input and rumble were verified over USB. Bluetooth has not been physically tested.
