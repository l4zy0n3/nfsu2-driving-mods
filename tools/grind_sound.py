# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Yash Tripathi and contributors
"""Generate our own deterministic, quiet grinding cue; no game audio is sampled."""
import math
from pathlib import Path
import random
import struct
import sys
import wave


def generate(path):
    rng = random.Random(2)
    rate, frames = 22050, 4410
    samples = []
    for i in range(frames):
        envelope = min(1, i / 110, (frames - i) / 220)
        buzz = math.sin(2 * math.pi * 160 * i / rate)
        samples.append(round(32767 * .10 * envelope * (.6 * rng.uniform(-1, 1) + .4 * buzz)))
    with wave.open(str(path), 'wb') as output:
        output.setparams((1, 2, rate, frames, 'NONE', 'not compressed'))
        output.writeframes(struct.pack('<' + 'h' * frames, *samples))


if __name__ == '__main__':
    generate(Path(sys.argv[1]))
