# BTstackA2DP

Bluetooth Audio A2DP **sink** (Bluetooth speaker) and **source** (Bluetooth transmitter) with AVRCP for Arduino,
built on the [BlueKitchen btstack](https://github.com/bluekitchen/btstack). The API follows
[ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP), so existing sketches port easily.

## Why this library?
Bluetooth audio on Arduino is usually tied to one chip, one vendor stack and one codec. BTstackA2DP
tries to fix that:

* **Lean:** the library itself is header-only C++ on top of one small C stack (btstack). Only SBC
  is built in; everything else is opt-in, so you only pay (flash, RAM, licences) for what you use.
  On the ESP32 it talks to the controller directly, without the much larger Bluedroid stack.
* **Multiple platforms:** one API for ESP32, RP2040 / RP2350 (Pico W) and the desktop
  (Linux / macOS with a USB dongle), plus a small port interface for other microcontrollers.
* **Multiple codecs:** SBC, AAC, aptX and MP3 (experimental), as plug-ins for the sink (decoder)
  and the source (encoder). You can add your own.
* **audio-tools integration:** codecs are taken from
  [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools), and its I2S output,
  buffers and streams plug in directly. The core does not depend on audio-tools, so it stays usable
  without it.

## Status
Hardware results so far (details in [docs/tests.md](docs/tests.md)):

| Platform | Controller | Verified on hardware | Not yet working / not tested |
|---|---|---|---|
| ESP32 (original) | internal (VHCI) | sink (SBC) receives from a PC; ESP32 source streams to an ESP32 sink, gap-free for minutes; reconnect after the sink resets | AVRCP metadata and remote control (E10, E12); aptX decoding; AAC and MP3 |
| RP2040 / RP2350 (Pico 2 W) | CYW43 | sink receives from an ESP32 source | source; WiFi coexistence |
| Linux / macOS | USB dongle (libusb) | start-up and shutdown work | connection to devices: the dongle used in tests is unreliable |

The Pico W build matrix was last checked before the codec additions.

## Documentation
| | |
|---|---|
| [Sink](docs/sink.md) / [Source](docs/source.md) | API and examples |
| [Codecs](docs/codecs.md) | SBC, AAC, aptX, MP3 and own codecs via arduino-audio-tools |
| [Build notes](docs/build.md) | ESP32, Pico W, desktop, new ports, updating btstack |
| [Tests](docs/tests.md) | test plan and tracker: what is verified, what is still open |

The `examples/` folder has ready-to-run sketches for each of them.

## License
Our code is MIT. **btstack is free for non-commercial use only**; commercial use needs a license
from BlueKitchen. See [LICENSE](LICENSE).
