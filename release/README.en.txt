Ninja Gaiden Sigma Enhancements 0.6.27 - Native Plugin TEST

The game now loads dinput8.dll through its normal DirectInput import. The core
starts on the first input API call, outside DllMain. This build contains no
remote-process injection code, resident autoloader, or startup registration.
It retains rumble, DualSense input/icons, and all afterimage materials/settings.
It remains unsigned. No antivirus clearance or Nexus approval is claimed.

Installation
1. Exit the game and extract the entire ZIP. Keep Payload beside the installer.
2. Run NGS1RumblePatch.exe outside the game's runtime folder and select the EXE.
3. Install/update Rumble, DualSense, and Afterimages separately as desired.
4. Open the game EXE normally or use the separate Launch Game button.
   Installation does not start the game. No resident patch program is needed.

Upgrade and restore
Tested upgrades from 0.6.25/0.6.26 preserve saved rumble rules and outfit settings.
The legacy compatibility runtime is migrated to the game directory and its
owned autoloader/Run entry/runtime folder are removed after successful install.
Features can be restored independently. Restore All restores the game-directory
files backed up by this installation and removes owned patch logs/backups.
It does not reinstall the old resident loader. Use a retained old package if
you deliberately want the previous loading design again.
Foreign dinput8.dll files and unknown runtime files are preserved and block
conflicting operations. Game EXEs, archives, saves and other mods are not patched.

Compatibility limits
Known 1.0.0.2/1.0.0.4 executable fingerprint checks are retained.
This round smoke-tested the known 1.0.0.2 EXE and the current imjinjia-localized
1.0.0.4 EXE. Direct EXE reopening also loaded the plugin. The independent original
1.0.0.4 build was not available as a full installation for a fresh startup test.
Do not describe this test as universally verified. See TEST-RESULTS.txt.

Features
All three afterimage materials and five costume presets remain, including
glow/color/opacity/muscle controls and the existing 200% controls. Auto/Standard/
Integrated GPU profiles, hot reload and all rumble/action timing rules remain.
DualSense USB input/rumble and PS/Xbox icon switching are retained. Bluetooth
has not been physically verified.

Diagnostics and review
Export Diagnostics collects the selected game's existing patch/crash logs.
Trace Game Crash explicitly runs the visible, verified NGS1CrashTrace.exe helper.
See SECURITY-REVIEW.txt for behaviors, BUILD-AND-SIGNING.txt for reconstruction,
PACKAGE-MANIFEST.json/SHA256SUMS.txt for inventory, and SOURCE-CHANGES.patch.
Checksums establish consistency, not trusted publisher identity or malware clearance.
