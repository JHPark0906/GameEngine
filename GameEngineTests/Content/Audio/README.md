# MP3 regression fixture

`synthetic-tone.mp3` is generated entirely from a mathematical sine wave. It contains no
recording, music sample, or other external audio. The Audio suite uses it for decoding,
importing, directory/packed content parity, decoder failure recovery, and silent playback
state checks. A missing or invalid fixture fails the suite.

- Source PCM: mono, 44,100 Hz, signed 16-bit little-endian, 88,200 samples (2 seconds).
- Signal: 440 Hz sine at amplitude `0.2 * 32767`, with 441-sample fades at both ends.
- Encoder: `lameenc` 1.8.4, 64 kbit/s, quality 2.
- File size: 16,300 bytes.
- SHA-256: `ca9f182a3d54022769760bf371dfc81f2764928905c9a6e5e41b657da557f560`.

The engine build and tests consume the checked-in MP3. Python and `lameenc` are needed only
when deliberately regenerating this fixture; they are not build or runtime dependencies.

To regenerate from the repository root, install `lameenc==1.8.4` into a Python 3 environment
and run:

```python
import math
import struct
from pathlib import Path
import lameenc

sample_rate = 44100
sample_count = 88200
pcm = bytearray()
for i in range(sample_count):
    envelope = min(1.0, i / 441.0, (sample_count - 1 - i) / 441.0)
    value = int(0.2 * 32767 * envelope * math.sin(2 * math.pi * 440 * i / sample_rate))
    pcm.extend(struct.pack("<h", value))

encoder = lameenc.Encoder()
encoder.set_bit_rate(64)
encoder.set_in_sample_rate(sample_rate)
encoder.set_channels(1)
encoder.set_quality(2)
encoded = encoder.encode(bytes(pcm)) + encoder.flush()
Path("GameEngineTests/Content/Audio/synthetic-tone.mp3").write_bytes(encoded)
```

See [validation instructions](../../../Docs/VALIDATION.md) for the Audio suite command.
