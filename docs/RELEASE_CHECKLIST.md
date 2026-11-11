# Stable release gates

Current version: `0.1.0-alpha.2`. Packaging and offline checks are preparation, not approval of physical driving behavior. No automatic public/stable release is configured.

- [x] Select GPL-3.0-or-later; identify the GPL-2.0-or-later DualSense references and preserve attribution.
- [ ] Complete the final release source/dependency provenance review.
- [ ] Confirm a fresh user can obtain the matching dependency and configure its loader layout without the developer's installation.
- [ ] Install, launch, drive, exit and uninstall on a clean supported Wine/Proton setup.
- [ ] Verify keyboard, XInput and wheel clutch/ignition bindings, disconnect/reconnect, focus loss and startup with no controller. Verify separate button-box claims only if support is added.
- [ ] Reproduce and close the reported handbrake hold/release issue in the game. The pure state-machine tests passing does not close the native integration bug.
- [ ] Verify neutral coasting on a slope, service-brake and handbrake holds, pause/countdown exclusions and automatic-mode handoff. The neutral patch has not been live-tested in this release checkout.
- [ ] Test free roam, circuit, sprint, drift and drag; verify R-N-1 ordering and native automatic/manual switching. Do not call drag fixed from model tests alone.
- [ ] Test stalling on/off, clutch-plus-ignition restart, bump start, clutch kick, grinding and paddle shifts on several cars.
- [ ] Validate Xbox rumble and DualSense trigger/haptic/speaker/LED output physically; verify immediate silence on focus loss, disconnect and stale telemetry.
- [ ] Validate touchpad/gyro and selected XInput slot with more than one input device present.
- [ ] Validate camera collision, mirrors, status text and D3D device reset across supported display modes.
- [ ] Measure frame time and a longer session on the supported stack. No 60 FPS guarantee follows from unit tests.
- [ ] Run `make verify` and `make package` on the exact release commit; archive CI result and checksums.
- [ ] Review the final archive and source tree for game assets, third-party binaries, local paths, device identifiers, secrets and personal modifications.

Record each live result with commit ID, dependency hashes, OS/Proton version, controller model/transport, reproduction steps and observed outcome. A test not performed remains unchecked. Native Windows and other executable builds require a separate support decision and validation.
