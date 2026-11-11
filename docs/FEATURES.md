# Release scope

| # | Feature | Purpose and boundary |
|---|---|---|
| 1 | Synchronizer clutch and gearbox | Model an independent input shaft, clutch coupling and synchronizer engagement while using the game's engine and wheel coefficients. |
| 2 | Clutch kick | Releasing the clutch transfers the model's stored shaft energy. No scripted speed boost is added. |
| 3 | Per-car clutch character | Authored bite-range and capacity profiles use each car's native torque and inertia. Values are gameplay estimates, not factory measurements. |
| 4 | Optional stalling and ignition | Low engine speed can stall combustion; restarting requires sufficient clutch disengagement. Input actions are remappable across keyboard, XInput and DirectInput wheel controls. |
| 5 | Grinding and paddle auto-clutch | Rejected shifts can give a generated grinding cue. Wheel paddles can request a simulated clutch cycle. No gearbox damage system is claimed. |
| 6 | Tap/hold handbrake | A short press toggles the handbrake. A hold of at least 350 ms is momentary and releases on letting go. Uses the game's mapped handbrake action. Live hold behavior is a release blocker. |
| 7 | Neutral coasting integration | Allows eligible human-driven manual cars in neutral to leave the game's resting state after brakes are released. It restores the native active solver rather than adding a push. Live slope validation is pending. |
| 8 | Native transmission integration | Preserve automatic/manual selection, native shift bindings, R-N-1 order and race-countdown handoff. Test each race mode before claiming full compatibility. |
| 9 | Standard controller rumble | Slip, clutch and grinding cues through XInput, with intensity limits and stale/focus/disconnect handling. |
| 10 | DualSense adaptive triggers | USB resistance cues for clutch and throttle. These are perceptual cues, not calibrated pedal forces. Use a trigger-based clutch mapping for this option. |
| 11 | DualSense haptics, LEDs and speaker | Four-channel procedural effects, gear/rev lights and small speaker cues derived from telemetry. No microphone capture or firmware changes. |
| 12 | Touchpad and gyro | Optional playerctl media commands and touch-activated gyro camera look. No bundled Spotify widget. |
| 13 | Right-stick camera look | Orbit/look using the selected XInput pad while retaining the native camera collision path. |
| 14 | Mirror and drivetrain display | Two crops of the existing rear-view image plus the clutch module's exported status. These are not independent mirror cameras. |

## Module boundaries

- `src/clutch/analog-clutch.c`: game hooks, input routing and status export; independent models live in adjacent headers.
- `src/clutch/manual_core.h`: synchronizer/drivetrain model; offline energy and state tests.
- `src/clutch/handbrake-core.h`: tap/hold state machine, tested independently of the native input adapter.
- `src/clutch/dualsense-native.h` and `rumble-native.h`: telemetry and XInput output adapters.
- `src/camera/`: two optional ASIs. The mirror module resolves `NFSU2RealismStatus` dynamically.
- `companion/`: USB reports, calibrated motion processing, procedural audio and bounded local IPC.

The dependency identity checks remain in `wheelfix-native.h`. The personal steering assistance formerly beside those checks has been removed.
