# Changelog

## 0.1.0-alpha.3 — lightbar protocol correction

- Reset LED control once when opening the USB controller. Send subsequent RGB/player-LED updates without restarting the built-in light-on animation.
- Keep native modules and clutch force tuning unchanged from alpha.2.

## 0.1.0-alpha.2 — controller feedback corrections

- Address the DualSense PipeWire sink by object serial rather than node ID, with a regression fixture where those values differ.
- Keep XInput steering on the game's native deadzone path instead of Wheel Fix's direct wheel-angle override. Wheel input remains unchanged.
- Replace the standalone controller-specific throttle fix with an opt-in binding in the main mod. It uses the selected XInput slot, accepts trigger/button bindings, and leaves native throttle as the package default.
- Release standard rumble ownership when the optional PCM transport fails or is disabled.
- Route adaptive trigger effects to the configured clutch/throttle triggers; leave unrelated triggers free.
- Accept Wheel Fix's native axis aliases and use binding-neutral stall prompts.


## 0.1.0-alpha.1 — release preparation

- Release the mod source as GPL-3.0-or-later, with corresponding source included in binary packages.
- Extract the fourteen agreed gameplay, feedback and camera features into a source-only repository.
- Remove the excluded personal physics, steering, camera, audio, unlock and maintenance fixes.
- Add generic remappable clutch/ignition controls and an independent handbrake enable switch.
- Replace personal paths and compositor-specific focus checks with private runtime IPC and native focus telemetry.
- Select DualSense audio by device identity and channel layout; use optional playerctl for touchpad media commands.
- Add reproducible package metadata, strict payload checksums, a preview-first installer with backup/restore, automated tests and GitHub CI.

The extracted neutral-coasting integration and handbrake changes still require live gameplay validation. This version is not a stable release.
