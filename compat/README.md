# Optional controller throttle compatibility fix

This is separate from the alpha. Enable it only for the known native controller throttle-scanner issue on the exact supported executable. It routes XInput slot 0 R2 to the native analog throttle event and clears the duplicate alternate throttle event. Native transmission logic is retained. It stops overriding throttle on disconnect; the recognized G923 path keeps its native scanners.

The local clean-install test exposed the need for this previously excluded compatibility correction. This module is not in the alpha.1 installer manifest. It does not generalize support to every wheel or XInput slot. Remove it from the scripts directory to disable it. Normal clutch, brake and shift mappings are configured separately.

Build from the repository root:

```sh
gcc -DSELF_TEST -O2 -Wall -Wextra -Werror compat/controller-throttle.c -o build/test_controller_throttle
./build/test_controller_throttle
i686-w64-mingw32-gcc -shared -O2 -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp,--disable-auto-image-base compat/controller-throttle.c -ldinput8 -ldxguid -luser32 -o build/NFSU2.ControllerThrottle.asi
```

Logs are written to `scripts/NFSU2.ControllerThrottle.log`. Source is GPL-3.0-or-later with the rest of this repository. No game bytes or third-party binaries are bundled.
