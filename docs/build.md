# Build notes

## ESP32
* Arduino-ESP32 3.3+ and a chip with Classic Bluetooth (the build checks `CONFIG_BT_CLASSIC_ENABLED`;
  the original ESP32 has it, S2/S3/C3/C6/H2/P4 do not).
* Uses the internal controller through VHCI, Bluedroid is not involved. The btstack task can be
  tuned with `BTSTACKA2DP_TASK_CORE`, `BTSTACKA2DP_TASK_STACK_SIZE` and `BTSTACKA2DP_TASK_PRIORITY`.
* Pairing information is stored in NVS.
* ESP32-S31: untested. It works only if the chip has Classic BT and the Arduino core exposes it;
  it is not in Arduino-ESP32 3.3.12.

## Raspberry Pi Pico W / Pico 2 W
* Select *Tools > IP/Bluetooth Stack > IPv4 + Bluetooth*.
* The btstack and CYW43 driver of the arduino-pico core are used (the vendored C files are skipped).
  Do not combine with the core's `BluetoothAudio` or `BTstackLib` libraries.
* The core's btstack allows one stream endpoint: extra codecs are skipped, SBC is used.

## Desktop (Linux / macOS)
Needs CMake, libusb and a USB Bluetooth dongle. The build produces one command line tool:
```
cmake -S . -B build && cmake --build build
./build/blue_a2dp help
```
| Command | Purpose |
|---|---|
| `blue_a2dp sink [options]` | receive audio from a phone / PC |
| `blue_a2dp source DEVICE [options]` | stream audio to the Bluetooth speaker named DEVICE |
| `blue_a2dp play FILE [options]` | decode a file the way the source does, no Bluetooth needed |
| `blue_a2dp test [sbc\|aac\|aptx\|mp3\|wav]` | self tests, no hardware needed (default: all) |

**Sink output** (can be combined; without any, 16 bit PCM goes to stdout):
`--play` speakers, `--wav FILE` WAV file (finished properly when you press Ctrl-C), e.g.
`blue_a2dp sink --usb 1:2 --wav recording.wav` or `blue_a2dp sink --usb 1:2 | aplay -f S16_LE -r 44100 -c 2`.

**Source input:** a 440 Hz tone (`--tone HZ`) or `--file FILE` (`.wav`; `.mp3` and `.aac` in the
audio-tools build), `--loop` to repeat it. The audio is converted to stereo and to the
sample rate negotiated with the speaker. The tool ends at the end of the file.

**Common options:** `--usb BUS:PORT`, `--name NAME`, `--bonding FILE` / `--no-bonding` (pairing data).
Messages go to stderr, so stdout can be piped.

* `--usb` is the bus and port of the dongle from `lsusb -t` (`1:2` is bus 1, port 2). **Always
  pass it**, otherwise the first controller is used, which may be your built-in one.
* libusb needs write access to the device node (udev rule or root); stop `bluetoothd` for that
  adapter. If no controller can be opened the tool says so and exits with code 1.
* WAV files must be 16 bit PCM. If the stream changes its sample rate while a WAV file is being
  written, the file keeps the rate of the first stream.
* Another HCI transport (e.g. UART) can be set with `PortPosix::set_transport()`.
* **Dongles:** use a genuine one (CSR8510 original, Realtek, Intel). A cheap CSR8510 clone
  (`0a12:0001` reporting HCI 5.1 / manufacturer 0x08E7) hung in our tests: its controller returned
  "Memory Capacity Exceeded" with no command credits, after which it needed a replug.
* **Debugging:** build with `-DBTSTACKA2DP_DEBUG` (`cmake -DCMAKE_CXX_FLAGS=-DBTSTACKA2DP_DEBUG ..`) to
  get an HCI trace and the connection log on stdout. On a microcontroller this prints to the
  serial port: use it only to find problems, as the logging slows the Bluetooth task down.

### Raspberry Pi (onboard Bluetooth)
The desktop tool also runs on a Raspberry Pi with Raspberry Pi OS, using the onboard Bluetooth
instead of a USB dongle (build as above, no `--usb`). The kernel and BlueZ bring the onboard
controller up as `hci0` at boot, including its firmware. The tool then takes the adapter over
through the HCI user channel, so BlueZ must release it first:
```
hciconfig -a                       # hci0 must exist
sudo systemctl stop bluetooth
sudo ./build/blue_a2dp sink --hci 0 --wav recording.wav
sudo systemctl start bluetooth     # when you are done: the adapter is down after the tool ends
```
`--hci N` needs root (or `CAP_NET_ADMIN`). If the bind fails with "Device or resource busy", BlueZ
still uses the adapter; with "No such device" there is no `hciN`. `--hci` and `--usb` cannot be
combined. The same option works for a USB dongle that BlueZ has loaded, not only for the onboard
chip. **Status:** not run on a Raspberry Pi yet (see [tests](tests.md), section 3.5).

### Playback and MP3 / AAC files (audio-tools build)
`--play` and `.mp3` / `.aac` files use [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
(MiniAudioStream and the libhelix decoders). On the desktop it needs the Arduino-Emulator and miniaudio. **Recommended: use local copies**
(for example the Arduino libraries you already have), as the audio-tools repository alone is
almost 1 GB:
```
cmake -S . -B build -DBTSTACKA2DP_AUDIO_TOOLS=ON \
  -DBTSTACKA2DP_AUDIO_TOOLS_DIR=~/Arduino/libraries/arduino-audio-tools \
  -DBTSTACKA2DP_HELIX_DIR=~/Arduino/libraries/codec-helix \
  -DBTSTACKA2DP_EMULATOR_DIR=/path/to/Arduino-Emulator \
  -DBTSTACKA2DP_MINIAUDIO_DIR=/path/to/folder-with-miniaudio.h
```
(`miniaudio.h` is also in `arduino-audio-tools/tests-cmake/miniaudio/`.) Any of the four that you
leave out is downloaded from GitHub (shallow clone of the `main` branch, several minutes for
audio-tools). That download path only has been checked up to the configure step, not built.
Without this option `--play` and MP3 / AAC files are rejected with a message.

`blue_a2dp play song.mp3 --play` is a quick way to check a file before sending it over Bluetooth;
`blue_a2dp play song.aac --wav song.wav` converts it (`--rate HZ`, default 44100).

### What has been tested
`play` with WAV (44.1 kHz stereo, 22.05 kHz mono, 96 kHz), MP3 and AAC files, checked against the
known 1 kHz test tone; WAV output including Ctrl-C; Miniaudio playback timing (silent file);
stdout output; all self tests. **Not yet tested with a real Bluetooth link:** `sink --wav`,
`sink --play` and `source --file` (they only connect the tested parts to the stack).

## Arduino Nano 33 BLE (nRF52840, mbed core)
Select `arduino:mbed_nano:nano33ble` (Arduino Mbed OS Nano Boards). The nRF52840 has no Classic
Bluetooth radio, so **A2DP needs an external HCI module on a UART**, for example a CSR8811
BlueCore module with the standard HCI firmware. Wiring:

| Module | Nano 33 BLE |
|---|---|
| VCC | 3.3 V (check the module's voltage) |
| GND | GND |
| RXD (input) | D1 (TX) |
| TXD (output) | D0 (RX) |
| RTS / CTS (optional, recommended) | any two free pins, see `set_uart()` |

* The default UART is the one of `Serial1` (D0/D1) at 921600 baud without flow control. A2DP needs
  at least about 921600 baud (115200 is too slow for SBC audio), so the module has to run at that
  speed. For other pins, flow control or baud rates call `set_uart()` before `start()`:
  ```cpp
  blue_a2dp::Port::set_uart(PIN_TX, PIN_RX, PIN_RTS, PIN_CTS, 921600);  // -1 = no flow control
  ```
* btstack runs in an mbed thread (16 KB stack); the sketch does not need to call anything else.
* `start()` waits until the module answers, at most 3 seconds, and returns false if it does not
  (not connected, wrong pins or baud rate). Change the time with
  `blue_a2dp::Port::set_startup_timeout(ms)` before `start()`.
* `printf` output (debug log with `BTSTACKA2DP_DEBUG`, fault reports) goes to the USB `Serial`:
  mbed would otherwise send it to D0/D1, the module's UART. A sketch can still pick another
  stream with `REDIRECT_STDOUT_TO(...)`.
* Pairing information is kept in RAM only: pairing is lost after a reset.
* Other nRF52 cores (Adafruit, Seeed) are not supported yet: they have no RTOS for the btstack
  thread. Sketches built for the nRF52840 without a Bluetooth module will not start.
* `sink-serial` and `source-sine` compile (about 18 % flash, 24 % RAM). **Not run on hardware yet.**

## Other microcontrollers
Add a port in `src/BTstackA2DP/ports/`; `Port.h` documents the four functions it has to provide
(`start`, `stop`, `post`, `lock/unlock`).

## Updating btstack
`extras/scripts/vendor_btstack.sh [path-to-btstack]` re-creates `src/btstack/`. Arduino only has
`src/` on the include path, so the script rewrites btstack's includes to relative paths
(`btstack_config.h` stays in `src/`). The version is recorded in `extras/BTSTACK_VERSION.txt`.

The official `btstack-arduino-latest.zip` cannot replace the vendored copy: it is a 2020 BLE-only
snapshot of an older API without A2DP, AVRCP or SBC.
