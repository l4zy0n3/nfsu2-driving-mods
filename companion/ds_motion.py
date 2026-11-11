# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
# DualSense protocol/calibration based on GPL-2.0-or-later hid-playstation and dualsensectl.
# Original reference copyright (c) 2020-2022 Sony Interactive Entertainment.
# See THIRD_PARTY_NOTICES.md for provenance and modification details.
"""Factory-calibrated acceleration and gravity-assisted camera pitch; no I/O."""
import math
import struct


def accel_calibration(report):
    if len(report) != 41 or report[0] != 5:
        raise ValueError('Invalid accelerometer calibration report')
    pairs = struct.unpack_from('<6h', report, 23)
    result = []
    for plus, minus in zip(pairs[::2], pairs[1::2]):
        span = plus - minus
        if not 4096 <= span <= 65535:
            raise ValueError('Invalid accelerometer calibration span')
        result.append((plus - span // 2, 2 / span))
    return result


class Gravity:
    def __init__(self):
        self.vector = None

    def update(self, accel, rates, dt):
        if not 0 <= dt <= .04 or not all(math.isfinite(v) for v in rates):
            self.vector = None
            return None
        norm = math.sqrt(sum(v * v for v in accel)) if accel is not None and all(math.isfinite(v) for v in accel) else 0
        measured = [v / norm for v in accel] if .85 <= norm <= 1.15 else None
        if self.vector is None:
            if measured is None:
                return None
            self.vector = measured
        else:
            x, y, z = self.vector
            wx, wy, wz = [math.radians(v) for v in rates]
            # Gravity is fixed in the world; body coordinates rotate opposite the gyro.
            predicted = [x + dt * (wz * y - wy * z),
                         y + dt * (wx * z - wz * x),
                         z + dt * (wy * x - wx * y)]
            length = math.sqrt(sum(v * v for v in predicted))
            predicted = [v / length for v in predicted]
            if measured is not None and sum(p * a for p, a in zip(predicted, measured)) < math.cos(math.radians(8)):
                measured = None  # Reject a gravity direction jump the gyro did not observe.
            alpha = 1 - math.exp(-dt / .75) if measured is not None else 0
            fused = [(1 - alpha) * p + alpha * a for p, a in zip(predicted, measured or predicted)]
            length = math.sqrt(sum(v * v for v in fused))
            self.vector = [v / length for v in fused]
        x, y, z = self.vector
        if y * y + z * z < .12:
            return None
        return math.degrees(math.atan2(-z, y))
