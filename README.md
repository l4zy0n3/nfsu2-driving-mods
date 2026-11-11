# NFSU2 Driving Mods

Original clutch, gearbox, handbrake, controller feedback and camera extensions for Need for Speed: Underground 2.

**Status: public FOSS alpha, `0.1.0-alpha.4`. Not a stable release.**
This checkout contains our mod source. The ASIs are 32-bit Windows binaries loaded by the existing game under Wine/Proton. The optional DualSense companion is a Linux program. This does not port or redistribute the game.

## Scope

The fourteen retained features are listed in [FEATURES.md](docs/FEATURES.md). The main module supplies a synchronizer clutch/gearbox, per-car clutch profiles, clutch kick, optional stalling/ignition, grinding, paddle auto-clutch, tap/hold handbrake, neutral coasting integration and feedback telemetry. Two optional ASIs provide camera look and mirror/status panels.

Input corrections use the selected input source and configurable bindings. Native throttle mapping is the default; installations with a combined-axis accelerator issue can opt into a selected XInput throttle binding. Gamepads retain the game's native steering/deadzone path. No controller model or personal calibration is baked into these corrections.

The alpha package excludes bespoke steering assistance, AWD changes, tyre/thermal extensions, extra drivetrain audio, RHD camera changes, unlocks, save edits, loading/traffic fixes, graphics wrappers and Spotify widgets. This repository contains no game assets, saves, Wine prefix, third-party mod binaries or proprietary decompilation exports.

## Compatibility

Only the exact executable and Wheel Fix dependency in [compatibility.json](compatibility.json) are accepted. Wheel Fix v1.0 must occupy the game's `dinput8.dll` path; its normal `scripts/ffb.asi` layout is not supported by this adapter yet. An ASI loader must also load `scripts/*.asi`. Follow the dependency's instructions and avoid loading Wheel Fix twice.

This adapter is not compatible with Bayview Forever's replacement physics integration. Other hook mods require explicit testing. Native Windows gameplay and other game builds remain unverified. The game and third-party dependencies must be obtained separately.

## Build and check

Linux tools: GCC, Make, Python 3.10+, NumPy and `i686-w64-mingw32-gcc` with the DirectX import libraries. On Ubuntu 24.04:

```sh
sudo apt-get install build-essential gcc-mingw-w64-i686 python3-numpy
make -j4 verify
make package
```

`make verify` runs the pure C model tests, Python companion/installer tests and ASan/UBSan checks, and builds all three ASIs. Tests do not open HID/audio devices or launch the game. `make package` produces an allowlisted ZIP plus SHA-256 checksum in `dist/`. ZIP entries have fixed timestamps; PE link timestamps are disabled. Byte reproducibility is verified with the same compiler toolchain, not promised across compiler versions.

## Use

Read [INSTALL.md](docs/INSTALL.md) and [CONTROLS.md](docs/CONTROLS.md). Installation is a dry run unless `--apply` is supplied. Existing configs are preserved; review them against the new controls before testing.

Default clutch/ignition: **V + I** on keyboard, **LB + right-stick click** on XInput gamepads. Wheels use their calibrated clutch axis and a remappable ignition button, with I as a keyboard fallback. Stalling is off by default. The handbrake uses the game's own binding.

Standard rumble uses XInput on the selected gamepad. The optional Linux DualSense companion supports USB product `054c:0ce6`. It requires readable/writable hidraw access, NumPy and PipeWire tools; `playerctl` enables optional touchpad media actions. Bluetooth and DualSense Edge are not enabled. Four-channel effects require a correctly configured DualSense audio sink. If that transport fails, standard rumble retains ownership. Adaptive trigger effects follow the configured LT/RT action bindings. The companion does not route the game's audio. Install the ASIs and companion from the same package version.

## Release status

Automated checks do not prove driving behavior or physical controller feel. See [RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) for the required live tests, known handbrake report and neutral-coasting validation. No stable release workflow is enabled. The project is licensed GPL-3.0-or-later. Distributed derivatives must retain the applicable copyleft terms; see [LICENSE.md](LICENSE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
