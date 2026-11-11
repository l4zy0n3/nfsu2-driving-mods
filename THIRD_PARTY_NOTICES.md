# Provenance and dependencies

The import allowlist and original file hashes are in `docs/import-manifest.json`. The imported files are the locally authored mod implementations and tests. No EA binary, game asset, save, proprietary decompilation export, Wheel Fix binary, ASI loader or reference-source file is included. Integration addresses and observed interface layouts are build-specific interoperability information.

The DualSense companion's protocol and calibration work references:

- Sony's Linux [`hid-playstation.c`](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c), marked GPL-2.0-or-later, copyright Sony Interactive Entertainment.
- nowrep's [`dualsensectl`](https://github.com/nowrep/dualsensectl), whose [`main.c`](https://github.com/nowrep/dualsensectl/blob/main/main.c) header is GPL-2.0-or-later and retains Sony's 2020 copyright. Its [license file](https://github.com/nowrep/dualsensectl/blob/main/LICENSE) supplies the GPL v2 text.

The Python report parsing, USB report construction and calibration in `companion/ds_core.py`, `ds_motion.py` and `bridge.py` were informed by those references. We conservatively retain their attribution and distribute the resulting work under GPL-3.0-or-later, using the references' "or later" permission. The local reference C file was not imported. Our adaptations add bounded telemetry, validation, gameplay effects, gestures, gravity filtering, Linux transport and per-user IPC; modifications were made in November 2026. Neither reference's unrelated device-management or firmware-update implementation is bundled.

`LICENSE` contains the complete GPL v3 terms. Source files carry SPDX notices. Release packages include corresponding mod source and build scripts under `source/`. This records the identified provenance; future contributions and dependency changes still require review.

External runtime/build dependencies are obtained separately: Wheel Fix v1.0, an ASI loader, the Windows/DirectX interfaces, GCC/MinGW-w64, Python, NumPy, Wine/Proton, PipeWire and optionally playerctl. Their respective licenses apply. No project license grants rights to redistribute those components.

Need for Speed and Underground 2 are Electronic Arts marks. DualSense is a Sony mark. Xbox is a Microsoft mark. This is an independent mod project, without affiliation or endorsement.

Built ASIs include compiler/runtime support. The MinGW-w64 notices and GCC Runtime Library Exception are reproduced in `licenses/`; the exception supplements the GPL text. These notices do not change the license of our mod source.
