# Controls

Change `scripts/NFSU2.Realism.ini`. Restart after changing `[Controls]` bindings. Throttle defaults to the game's mapping. Brake, steering, gears and handbrake remain configured in-game or in the dependency's setup tool. Do not bind two different actions to the same control.

| Action | Keyboard default | XInput default | Wheel / button box |
|---|---|---|---|
| Clutch | V (`ClutchKey=86`) | LB (`GamepadClutch=LB`) | Calibrated `ffb.ini` clutch axis |
| Ignition | Hold I (`IgnitionKey=73`) with clutch down | Hold right-stick click (`GamepadIgnition=RTHUMB`) with clutch down | `WheelIgnitionButton=0` disables a wheel button; set a one-based button number, or use I |
| Realism toggle | F7 (`ToggleKey=118`) | Keyboard fallback | Optional `WheelToggleEnabled` combo |
| Handbrake | Native game binding | Native game binding | Native game binding |
| Shift | Native game binding | Native game binding | Existing H-pattern mapping or native sequential events |

`Input=Auto` selects an available configured wheel, then the configured XInput slot, then keyboard. `Input=Keyboard`, `Gamepad` or `Wheel` fixes the source. Source selection is made at startup. Disconnecting does not silently move clutch control to a different pad; restart to change devices.

`GamepadIndex=0` selects XInput slot 0 (valid range 0–3). Clutch, rumble and right-stick look use that slot. Gamepad bindings accept uppercase `LT`, `RT`, `LB`, `RB`, `LTHUMB`, `RTHUMB`, `A`, `B`, `X`, `Y`, `BACK`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT`. `NONE` disables gamepad ignition. Trigger clutch bindings are progressive; buttons and keyboard are binary. Keyboard keys use decimal Windows virtual-key codes; 0 disables ignition or the toggle. Invalid bindings stop hook installation and leave a diagnostic log.

An ignition binding of `START` is supported only on slot 0. With the clutch down, the mod consumes that ignition press until Start is released; an ordinary Start press still pauses. The default RTHUMB binding avoids needing to intercept pause.

For a progressive left-trigger clutch on any XInput-compatible gamepad, use `GamepadClutch=LT`. Rebind the game's brake to another control first. The default LB clutch leaves LT available for the native brake; also check that LB does not overlap a native shift binding. Neither layout is forced globally.

`GamepadThrottle=Native` preserves the in-game throttle mapping. If the native combined-axis path causes unwanted throttle, set this to the desired XInput binding, such as `RT`. The correction uses `GamepadIndex`, produces zero on disconnect, and suppresses the duplicate alternate throttle event. Keyboard and wheel throttle stay native. An override cannot share the clutch or ignition binding. Uninstall the superseded standalone `NFSU2.ControllerThrottle.asi` before using this integrated correction; conflicting hooks fail the build guard.

For gamepad steering, the mod disables Wheel Fix's direct wheel-angle override so the game's controller/deadzone path applies. Configure the deadzone in the game/input layer or Widescreen Fix, if installed. The mod does not ship a personal center offset or force a deadzone percentage.

## Wheels and button boxes

Configure and calibrate the wheel using Wheel Fix's setup tool. Its `[map] clutch` value is `GUID|AXIS|released|pressed`. Supported axes: `X`, `Y`, `Z`, `RX`, `RY`, `RZ`, `SLIDER0`, `SLIDER1`, including the setup tool's `Rx`, `Ry`, `Rz`, `S0`, `S1` aliases. Endpoints must be distinct and in 0–65535. The device GUID belongs to the user's installation and is never shipped in this repository.

H-pattern mappings and wheel paddle auto-clutch use the existing Wheel Fix shifter configuration on the clutch device. Without that H-pattern profile, native sequential shift events are used. Bind ignition to a button on that device, or use any keyboard-emulating button box. A separate raw DirectInput button box for ignition is not implemented; do not describe this alpha as supporting every device combination.

Extra wheel-to-key mappings are disabled by default. If enabled, Camera/LookBack/Pause inject the game's default C/B/Escape keys; custom in-game bindings must be matched manually.

## Effects and gestures

Standard rumble uses the configured XInput slot. Optional DualSense output requires `[DualSense] Enabled=1` and the matching companion. Clutch and throttle trigger effects follow their configured `LT` or `RT` bindings; keyboard, button and native/unknown throttle mappings do not impose resistance on an unrelated trigger. LEDs, gyro and adaptive triggers do not alone suppress standard rumble. The companion claims motor ownership only while its PCM transport runs, and releases it on failure or inactivity. Mixed-controller routing for the raw USB companion remains unverified; test with a single DualSense as the selected gamepad.

`Effects.Stalling=0` is the default. Set it to 1 to test stalling and clutch-plus-ignition restarts. `Handbrake.Enabled=0` restores native handbrake handling. A tap toggles; a hold of at least 350 ms releases on button release. Release the control after focus loss or reconnect before using it again.

The Linux companion's touchpad left area provides tap play/pause and swipe next/previous via playerctl. The right area activates gyro look while held. The former widget-toggle gesture has no action in this package. `companion/bridge.py set <setting> <JSON-value>` adjusts supported effects; `status` reports state. Hardware amplitude and gesture feel still require validation.
