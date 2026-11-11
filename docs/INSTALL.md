# Installation and removal

Use a test installation first. Close the game before applying changes. The release package does not include a game executable, ASI loader, Wheel Fix, DirectX runtime, save or graphics mod.

1. Use your own matching game executable. The exact hashes are in `compatibility.json`.
2. Obtain [Wheel Fix v1.0 from its author](https://www.nexusmods.com/needforspeedunderground2/mods/115?file_id=160&tab=files). This adapter requires that binary as `dinput8.dll` in the game root. The checked original `scripts/ffb.asi` binary has SHA-256 `9f48dc0a498f9c3e17bf734519d83abd6fbb93acddac899596ab4a1e751482de`. A newer Wheel Fix build is not interchangeable. Keep a working ASI loader and do not load a duplicate `ffb.asi`.
3. Ensure the existing loader loads `scripts/*.asi`. Mirror text imports D3DX9; a working 32-bit D3DX9 runtime is required. The package does not install or modify loader/runtime dependencies.
4. Extract the release ZIP into a separate directory. Check its `.sha256` and internal `SHA256SUMS`.

The complete matching mod source and build scripts are included under `source/`. Run build commands from that subdirectory when using the ZIP.

From the extracted package:

```sh
python3 install.py --game /path/to/NFSU2
python3 install.py --game /path/to/NFSU2 --apply --accept-prerelease
```

The first command previews changes. The second backs up replaced files and installs only the six named payload files. Existing INIs are preserved; compare them with the packaged versions, especially `[Controls]`, `[Handbrake]` and `[DualSense]`. The installer does not edit `ffb.ini` or saves. Backups and the recovery journal live in `.nfsu2-driving-mods/` under the game directory. Do not remove that directory before uninstalling.

This alpha installs all three ASIs. After installation, optional camera ASIs may be archived outside `scripts`, but restore them before using the tracked uninstaller. The installer refuses changed/missing files rather than overwriting edits. Use a future version's selective installer only when it is implemented; there is no module-selection flag today.

To preview removal, then restore the original files:

```sh
python3 install.py --game /path/to/NFSU2 --uninstall
python3 install.py --game /path/to/NFSU2 --uninstall --apply
```

Newly installed files are archived and previous files are restored. Config files created by this installer are also tracked; archive your edits before restoring them to the packaged content if uninstall reports a conflict. Existing configs were never tracked or overwritten. Upgrade by uninstalling first, then installing the new package. An interrupted installation can be recovered with the same uninstall command.

## Optional Linux DualSense companion

USB `054c:0ce6` only. Install NumPy (`companion/requirements.txt`), `pw-cat`/`pw-dump` and optionally `playerctl`. Give the desktop user access to the device's hidraw node through the distribution's normal controller permissions. Do not run the bridge as root. Select a four-channel DualSense output profile through your normal PipeWire session policy. This package does not change audio routing or profiles.

Set `[DualSense] Enabled=1` and choose the appropriate bindings. Run your existing **foreground, blocking** Wine/Proton command through:

```sh
python3 companion/run.py -- wine /path/to/NFSU2/SPEED2.EXE
```

Retain your usual prefix/environment. Replace the Wine command with the actual blocking command you use. A launcher that exits immediately will also stop the companion. The wrapper creates private per-user IPC files, passes their Windows paths to the ASIs, and stops its bridge when the command exits. It assumes Wine's normal `Z:` mapping to `/`.

The bridge reads calibration from the controller and releases its effects on stale input/focus loss. Four-channel PCM is sent only to one identified USB DualSense sink with FL/FR/RL/RR order. Missing or ambiguous hardware disables that output. Game audio remains on its existing device. Bluetooth, Windows companion support and multiple simultaneous DualSense devices are outside this alpha's supported scope.

Logs: `scripts/NFSU2.Realism.log`, `scripts/NFSU2.RightStickLook.log`, `scripts/NFSU2.MirrorPanels.log`; companion logs live in `/tmp/nfsu2-driving-mods-<uid>/bridge.log`. Review logs before sharing them.
