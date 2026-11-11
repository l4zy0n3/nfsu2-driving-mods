# Changelog

## 0.1.0-alpha.1 — release preparation

- Release the mod source as GPL-3.0-or-later, with corresponding source included in binary packages.
- Extract the fourteen agreed gameplay, feedback and camera features into a source-only repository.
- Remove the excluded personal physics, steering, camera, audio, unlock and maintenance fixes.
- Add generic remappable clutch/ignition controls and an independent handbrake enable switch.
- Replace personal paths and compositor-specific focus checks with private runtime IPC and native focus telemetry.
- Select DualSense audio by device identity and channel layout; use optional playerctl for touchpad media commands.
- Add reproducible package metadata, strict payload checksums, a preview-first installer with backup/restore, automated tests and GitHub CI.

The extracted neutral-coasting integration and handbrake changes still require live gameplay validation. This version is not a stable release.
