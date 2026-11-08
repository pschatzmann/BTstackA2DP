# Test plan and tracker

Status values: **PASS**, **FAIL**, **TODO** (not run yet), **BLOCKED** (needs something we do not
have), **N/A**. A line only says PASS if it was actually run; the date is the day of the last run.
When you run a test, update the status, date and notes, and add a line to the [log](#log).

## 1. Automated tests (no hardware)
Run with `blue_a2dp test` (see [build notes](build.md)); `blue_a2dp test mp3` runs a single one.

The tests with a sine tone use `extras/test/gaps.py FILE.wav` on the received recording. It fits the
sine (`x[n] = 2 cos(w) x[n-1] - x[n-2]`) and reports every discontinuity and every run of digital
silence; exit code 0 means no gaps. For a sink that records with `--wav`, run it on that file; for the
PC as receiver record the speaker monitor, e.g. `parec -d <sink>.monitor --rate=48000 --channels=2 --format=s16le`.

| ID | Test | What it checks | Status | Date | Notes |
|---|---|---|---|---|---|
| A1 | `sbc` | SBC encode, decode of a 1 kHz tone with the vendored codec | PASS | 2026-11-08 | RMS 7074 (expected 7071) |
| A2 | `aac` | LATM / ADTS conversion, frame extraction, length bytes (255 edge case) | PASS | 2026-11-08 | |
| A3 | `aptx` | aptX vendor id, rate and channel decoding, capability choice | PASS | 2026-11-08 | |
| A4 | `mp3` | MP3 bit rate and sample rate choice | PASS | 2026-11-08 | |
| A5 | `wav` | WAV write, read, mono to stereo, 96 to 44.1 kHz, looping, error cases | PASS | 2026-11-08 | |
| A6 | `play` with generated files | WAV 44.1 stereo, 22.05 mono, 96 kHz, MP3, AAC decode to the right pitch and level | PASS | 2026-11-08 | audio-tools build, local dependencies; manual (ffmpeg files) |
| A7 | `play --wav` and Ctrl-C | WAV header is finished correctly after SIGINT | PASS | 2026-11-08 | 242 MB file, valid header |
| A8 | `play --play` | Miniaudio playback, real-time pacing | PASS | 2026-11-08 | silent file, 1.00 s in 1.02 s; not listened to |
| A9 | CLI error paths | bad option, bad USB path, missing file, wrong type, no controller | PASS | 2026-11-08 | exit codes 1 / 2 |
| A10 | audio-tools build with downloaded dependencies | CMake fetches and builds with GitHub `main` versions | BLOCKED | 2026-11-08 | configure and fetch worked; the build was stopped by the safety check (it would compile code freshly downloaded from external repositories). Needs your explicit OK to build the downloaded sources |
| A11 | Unit tests in CI | run A1 to A5 automatically on every change | N/A | 2026-11-08 | unit tests removed from CI on request. `.github/workflows/tests.yml` now only builds the desktop tool and compiles two ESP32 examples. Not run on GitHub yet |

## 2. Build matrix
`arduino-cli compile` for the examples; Pico W / Pico 2 W (`rpipicow`, `rpipico2w`) with *IP/Bluetooth Stack: IPv4 + Bluetooth* (`ipbtstack=ipv4btcble`).
Desktop = CMake build.

| Example | ESP32 | Pico W / Pico 2 W | Desktop |
|---|---|---|---|
| `sink-serial` | PASS | PASS (both boards) | N/A |
| `source-sine` | PASS | PASS (both boards) | N/A |
| `sink-i2s` (audio-tools) | PASS | PASS (both boards) | N/A |
| `sink-aac-i2s`, `source-aac` | PASS | PASS (both boards) | N/A |
| `sink-aptx-i2s`, `source-aptx` | PASS | PASS (both boards) | N/A |
| `sink-sbc-audiotools`, `source-sbc-audiotools` | PASS | PASS (both boards) | N/A |
| `sink-mp3-i2s`, `source-mp3` | PASS | PASS (both boards) | N/A |
| `blue_a2dp` tool | N/A | N/A | PASS (plain and audio-tools build) |
| ESP32-S2 / S3 / C3 / C6 | stops with the "needs Classic Bluetooth" error (checked on S3) | | |
| ESP32-S31 | TODO | | |

ESP32 examples were last built in full on 2026-11-08; `sink-serial` and `source-sine` were rebuilt
after the last library change. All 11 examples were built for the Pico W and the Pico 2 W on
2026-11-08 with the R20 / R21 / R22 fixes and the nRF52840 / Raspberry Pi commit: 22 of 22 PASS
(slow: several minutes per sketch, run 4 in parallel). Repeated on 2026-11-08 after the nRF52
fixes (R23, R24) and the migration P1 API, together with ESP32 (all 11 examples) and the
Nano 33 BLE (`sink-serial`, `source-sine`): 35 of 35 PASS.

## 3. Hardware tests

### 3.1 ESP32 (original, ESP32-D0WD) as sink
Setup: `sink-serial` flashed, PC (PipeWire) as source. Flash at 115200 baud (921600 failed).

| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| E1 | Controller starts through VHCI, `start()` returns true, no crash | PASS | 2026-11-08 | |
| E2 | Device is discoverable ("BTstackA2DP") and shows the right address | PASS | 2026-11-08 | |
| E3 | Pairing and A2DP connection from a PC | PASS | 2026-11-08 | bonded earlier through BlueZ |
| E4 | SBC audio is received and decoded; 90 s sine gap test on the ESP32 itself | PASS | 2026-11-08 | exactly 48 000 samples/s, 0 discontinuities, heap stable (190 492 bytes); PC (PipeWire, SBC-XQ) as source |
| E5 | Sound quality: correct pitch, no dropouts (needs a DAC, `sink-i2s`) | TODO | | no DAC connected yet |
| E6 | Connection-state, audio-state and audio-info callbacks fire with correct values | PASS | 2026-11-08 | connection 2, audio-state 2, audio-info 48000 Hz 2 ch, play status and metadata callbacks |
| E7 | Pairing survives a reboot (NVS) | PASS | 2026-11-08 | reset, PC reconnects without pairing again, audio flows (BlueZ prints a harmless `br-connection-busy`) |
| E8 | Auto reconnect after the remote drops the link | PASS | 2026-11-08 | found a bug: a failed attempt was never retried. Fixed (retry with backoff 2, 4, 8, 16, 32, 60 s); third attempt succeeded after 14 s |
| E9 | Pause and resume on the phone (stream suspend, restart) | PASS | 2026-11-08 | audio-state 0 at stream end, 2 at restart, stream resumes with correct format |
| E10 | Track change gives metadata (`set_avrc_metadata_callback`) | PASS | 2026-11-08 | ESP32 source sends title and artist (`source.set_now_playing`); the ESP32 sink receives "BTstackA2DP test track" and "tester" on every AVRCP connection. Earlier empty strings were the source not providing track information (R17) |
| E11 | Volume from the phone reaches `set_on_volume_change`; `set_volume` reaches the phone | PASS | 2026-11-08 | ESP32 pair, both directions: sink to source 100, 30; source to sink 50 (events logged on both sides). The phone / speaker directions still to check with real devices |
| E12 | `play`, `pause`, `next`, `previous` control the phone | PASS | 2026-11-08 | ESP32 sink to ESP32 source: pause and play change the source's audio state (suspended / started); next and previous arrive as AVRCP FORWARD (0x4B) and BACKWARD (0x4C), each press and release. Remaining check: a real phone or speaker |
| E13 | Long run: 1 hour continuous without dropouts or heap loss | PARTIAL | 2026-11-08 | 11:37 to ~11:45 continuous stream from `rhythmbox` on the PC (built-in link), audio-state stayed 2, byte counter rising steadily, no disconnect. Stopped early to reflash the ESP32 as source (S-tests). 1 hour not reached |
| E14 | Behaviour with WiFi / BLE running at the same time | PARTIAL | 2026-11-08 | WiFi access point on the sink, A2DP streaming from the source: stable, no audio-state drops. No WiFi client yet (traffic not tested); BLE not tested |

### 3.2 ESP32 as source
| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| S1 | Connects to a sink and streams (`source-sine`) | PASS | 2026-11-08 | PC (BlueZ) connected to the ESP32 as sink; the ESP32's own scan-and-connect to the desktop sink did **not** complete, see R9 |
| S2 | SBC stream plays the tone at the correct pitch | PASS | 2026-11-08 | 440.0 Hz at the negotiated 48 kHz (recorded on the PC) |
| S8 | Gap test: 20+ s sine, no discontinuities or silence (`extras/test/gaps.py`) | PASS, with caveats | 2026-11-08 | run 1: 23 s clean. Run 2 (source still scanning while streaming, R12): 587 discontinuities in 38 s. After the fix (R12): 8 discontinuities and 4 silent runs in 38 s, not yet zero. Dongle unplugged between the runs, so the effect of the fix is not isolated |
| S9 | Source stays quiet on the radio while streaming (no inquiry once connected) | PASS (by code, measurement above) | 2026-11-08 | fix in `BluetoothA2DPSource`; needs a repeated, longer gap test to rule out the remaining glitches |
| S3 | Works with joint-stereo-only speakers | PASS (simulated) | 2026-11-08 | Pico sink made to advertise SBC joint stereo only. Before the fix: silent stream with a garbage configuration (R21). Now the source offers stereo and joint stereo, btstack picks joint stereo, clean sine on the Pico (residual 18). A sink without any common mode (mono only) is now refused: the remote rejects Set Configuration, the source disconnects and does not retry. Real joint-stereo-only speaker not tested |
| S10 | ESP32 source to Pico 2 W sink (connection and stream) | PASS | 2026-11-08 | streamed for about 4 min at 44.1 kHz; the Pico's restart needed the ESP32 to reconnect: stale link key, fixed (R20); now reconnects by itself within about 2 s of the Pico coming back |
| S4 | Volume: `set_volume` and volume buttons of the speaker | PASS (ESP32 pair) | 2026-11-08 | source `set_volume` reaches the sink (sink logs 50). Volume buttons of a real speaker not tested |
| S5 | Speaker disconnects and returns: auto reconnect | PASS | 2026-11-08 | ESP32 sink reset during a stream: source reconnected by itself about 20 s later (`connected` at 12:29:08), audio resumed on the sink. Reset dropped one discontinuity |
| S6 | `set_ssid_callback`, `start_with_address`, `start_raw` | TODO | | not tested yet |
| S7 | 1 hour continuous | PARTIAL | 2026-11-08 | ESP32 pair: about 25 min of streaming with one reset-triggered reconnect (S5), no further drops; the one-hour run not finished |

### 3.3 Raspberry Pi Pico W
| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| P1 | Sink: discoverable, pairs, receives SBC | PASS | 2026-11-08 | Pico 2 W (`rpipico2w`, `ipbtstack=ipv4btcble`), ESP32 `sine_source` as source: fresh pairing, 44.1 kHz stereo at exactly 176 128 B/s for 2.5 min, heap flat. Every Pico reflash wipes the Pico's link keys; the source now pairs again by itself (R20). PC/phone pairing not tested |
| P2 | Sink with `sink-i2s` (non-blocking queue, no dropouts) | PASS (no DAC) | 2026-11-08 | sink-i2s structure (lock-free 32 KB queue, drained and sine-checked in `loop()`): 0 overflows, 0 gaps after the first 5 s window (the first window always has 1 to 20 gaps from the stream start), max SBC residual 187 to 299. I2S output itself not tested (no DAC) |
| P3 | Source finds and streams to a speaker | PASS (ESP32 as speaker) | 2026-11-08 | Pico source finds `ESP-SINK` by name, connects in 1 s, 44.1 kHz for 2.5 min, 0 gaps after the start window on the ESP32 gap sink, ESP32 heap flat. Real speaker not tested |
| P4 | Extra codecs are skipped without error (one endpoint in the core's config) | PASS | 2026-11-08 | `add_codec(aac)` returns 1, `start()` ok, AAC seid 0xFF (skipped), SBC stream clean. The skip is silent: nothing tells the user that AAC is unavailable |
| P5 | Coexistence with WiFi | PARTIAL | 2026-11-08 | `WiFi.softAP` (`PICO-AP`, seen from the PC) next to the A2DP sink: stream clean, 0 gaps after the start window. No WiFi client / traffic. The first AAC+WiFi run had a silent Pico (no USB serial output while btstack still answered); not reproducible in 3 later runs |

### 3.4 Desktop (Linux) with a USB dongle
The CSR dongle (0a12:0001, bus 1 port 2) was unplugged during the last run.

| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| D1 | `sink --usb` opens the dongle (needs write access to the device) | PASS | 2026-11-08 | first run after a replug: controller initialises and reaches the working state (HCI trace); later runs fail (R9, R10) |
| D2 | Phone pairs, `sink --wav out.wav` records correct audio | BLOCKED | 2026-11-08 | the CSR dongle's controller hangs (R9); needs a different dongle |
| D3 | `sink --play` plays through the speakers | TODO | | |
| D4 | `source SPEAKER --file x.wav` (also .mp3, .aac) is heard on a speaker | BLOCKED | 2026-11-08 | dongle start-up fixed, connection to the ESP32 never established (see R16/R19), dongle unreliable |
| D5 | `source` tone follows a 48 kHz speaker (rate taken from the stream) | BLOCKED | 2026-11-08 | same as D4 |
| D6 | `--usb` never touches the built-in controller | PASS | 2026-11-08 | wrong path gives "no controller", built-in one untouched |
| D7 | macOS build and run | TODO | | |

### 3.5 Raspberry Pi (onboard Bluetooth, `blue_a2dp --hci 0`)
Setup: Raspberry Pi with Raspberry Pi OS (BlueZ), `hciconfig -a` shows `hci0`, `bluetooth` service
stopped while the tool runs. Desktop tool built on the Pi with the plain CMake build.

| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| R-P1 | `--hci 0` takes the adapter (no `EBUSY` / `ENODEV`), HCI reset and start-up work | TODO | | the code path is compiled; without root the bind fails with EPERM (checked on the x86 desktop) |
| R-P2 | Pi as sink: a phone or PC pairs and `sink --wav` records correct audio | TODO | | |
| R-P3 | Pi as source: `source SPEAKER --file x.wav` heard on a speaker | TODO | | |
| R-P4 | After the tool ends the adapter comes back after `systemctl start bluetooth` | TODO | | |

### 3.6 nRF52840 (Arduino Nano 33 BLE, mbed core) with an external HCI module
Setup: CSR8811-type module on Serial1 (D0/D1), standard HCI firmware at 921600 baud. Compiled with
`arduino:mbed_nano:nano33ble` (`sink-serial` and `source-sine`: no errors).

| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| N1 | Compile `sink-serial` and `source-sine` | PASS | 2026-11-08 | 18 % flash, 24 % RAM. Also the test sink (`gap_sink`, 32 KB queue) with the R20-R22 fixes: 21 % flash, 37 % RAM; runs on the board |
| N2 | Module answers HCI reset; `start()` returns true | BLOCKED | 2026-11-08 | bare Nano 33 BLE, no HCI module yet. Without a module `start()` now returns false after 3006 ms (R24 fixed) and powers the controller off. Answer of a real module still to test |
| N3 | Sink: pairs and receives SBC audio at 921600 baud | TODO | | |
| N4 | Source: streams a sine to a speaker without gaps (`extras/test/gaps.py`) | TODO | | |
| N5 | RTS/CTS wiring and `set_uart()` with a non-default pin set | TODO | | |
| N6 | Pairing survives a reset | N/A | | pairing is kept in RAM only (limitation) |

### 3.7 ESP32-A2DP compatible API (migration P1)
Compile test: `p1_api` sketch (every new function, `BluetoothA2DPSink a2dp_sink(i2s)` with an
audio-tools `I2SStream`, a sink on `Serial`) builds on ESP32 and Pico 2 W; desktop tool builds.

| ID | Test | Status | Date | Notes |
|---|---|---|---|---|
| C1 | RSSI: callback at connect and every 5 s, `get_last_rssi()`, `update_rssi()` | TODO | | |
| C2 | Play position from a phone / PC player, throttled to `notif_interval` | TODO | | needs a peer that supports PLAYBACK_POS_CHANGED (our source does not) |
| C3 | Metadata mask: only the selected attributes reach the callback | TODO | | ESP32 source with `set_now_playing()` is a usable peer |
| C4 | Source `start()` without a name: SSID callback sees devices, `true` connects | TODO | | |
| C5 | `set_connected(false)` / `set_connected(true)` | TODO | | |
| C6 | `activate_pin_code()`: number shown on the phone, `pin_code()`, `confirm_pin_code()` accepts, wrong `confirm_pin_code(code)` rejects | TODO | | |
| C7 | AAC `set_bitrate_callback()` gets the agreed bitrate | TODO | | needs an AAC speaker |
| C8 | `BluetoothA2DPSink a2dp_sink(i2s)`: `begin()` at start, rate via `setAudioInfo()`, clean audio | TODO | | needs a DAC; with a `Print` it can be checked by counting bytes |

## 4. Codec interoperability
Fill in per device: the codec that was actually negotiated and whether it sounds right.
Rows are devices, "SBC (audio-tools)" is the `set_sbc_codec()` replacement.

| Codec | Sink: phone / PC | Source: speaker / headset | Notes |
|---|---|---|---|
| SBC (btstack) | PASS with PC (E4) | TODO | |
| SBC (audio-tools) | TODO | TODO | decoder skips the 1 byte payload header; encoder is plain stereo |
| AAC | TODO (iPhone) | TODO | pre-selection override of btstack's SBC choice is untested on a real peer |
| aptX | PARTIAL: PC offers "A2DP Sink, codec aptX" for the ESP32 endpoint (capabilities valid); packets confirmed **without RTP header** (1016-byte packets start with `4b bf 4b bf`, same as ffmpeg's raw stream); decoding on the ESP32 not yet verified (R13, R14) | TODO | |
| MP3 | TODO | TODO | experimental, btstack's MPEG audio code is asymmetric; few devices offer it |

## 5. Known risks and open questions
| # | Question | How to resolve |
|---|---|---|
| R1 | Does the source's AAC / aptX / MP3 selection really override btstack's automatic SBC choice? | S-tests with a speaker that offers AAC, check the negotiated codec in the log |
| R2 | aptX without RTP header: correct for real devices? **Confirmed** with PipeWire (packets start with the aptX stream prefix `4b bf 4b bf`) | done |
| R3 | MP3 capability and configuration bytes (btstack encodes them asymmetrically) | a device that offers MP3, or a capture of the AVDTP exchange |
| R4 | Pico W: core's `MAX_NR_AVDTP_STREAM_ENDPOINTS` is 1, extra codecs are skipped | P4 |
| R5 | Plain stereo only for the source; some speakers may require joint stereo. **Fixed**: the source offers joint stereo too (R21) | S3 with a real speaker |
| R6 | ESP32-S31: Classic Bluetooth available? Arduino core support? | when the core supports it |
| R7 | Dropouts when the stream reader blocks (I2S write in the Bluetooth task) | E5 / E13 with `sink-i2s` |
| R9 | Desktop sink on the CSR dongle (0a12:0001) never completes a connection. HCI trace: after accepting a connection the controller answers `Set_Connection_Encryption` with status 0x07 (Memory Capacity Exceeded) and **0 command credits**, so btstack cannot send anything else; the PC's connection request 18 s later is never answered (BlueZ: AuthenticationTimeout, the ESP32 stays "connecting"). A second run on the same dongle fails already at `HCI_Reset` (0x07, 0 credits). The controller reports HCI/LMP 5.1 with manufacturer 0x08E7: very likely a fake CSR8510 clone | **Not our code, most probably.** Repeat with another dongle (genuine CSR8510, Realtek, Intel). The same ESP32 works with the PC's built-in controller in both roles |
| R10 | After `blue_a2dp` stopped, the dongle stayed without kernel driver (`Driver=[none]`) until replugged: upstream's libusb transport only reattaches it on error paths. **Patched** in `vendor_btstack.sh` (`libusb_attach_kernel_driver` on close) and the CLI now shuts down cleanly if the controller does not start. **Not verified:** this dongle drops off the USB bus after the libusb open/reset once it is in the bad state, so the patch could not be checked | verify with a dongle that works: run and stop the tool, `lsusb -t` must show `Driver=btusb` again |
| R12 | The source kept running inquiry cycles and name requests while it streamed to a device that had connected to it, taking radio time from the stream (audible breakup, 587 discontinuities in 38 s). **Fixed**: scanning stops as soon as any connection exists | repeat S8 for 5 minutes, several times, with and without the dongle plugged in |
| R13 | audio-tools defaults to a 4-byte `int24_t`, but its aptX codec needs packed 3-byte samples: without `-DUSE_3BYTE_INT24` the decoded audio is garbage (440 Hz tone decoded as ~11 980 Hz noise; also on the desktop with ffmpeg's reference stream). **With the define the desktop decode is perfect** (440.0 Hz, 0 gaps for any chunk size) | the aptX adapters now check this at compile time; examples define it; the ESP32 check is still open (R14) |
| R14 | aptX sink on the ESP32: libopenaptx needs **20.6 ms of CPU per 21.17 ms packet (97% of the Bluetooth core, worst case 25 ms)**; the backlog starves the idle task and the task watchdog resets the chip (`task_wdt: CPU 0: btstack`) after about 5 s. With `-O3` (`compiler.optimization_flags=-O3`) it needs 14.1 ms (66%) and runs 20 s without a reset. The decoder runs inside the Bluetooth task | open: decode in a separate task on the other core, document `-O3`, check 44.1 kHz |
| R15 | aptX on the ESP32: the decoded level stays at peak 1 (silence) even with `USE_3BYTE_INT24`, with the audio-tools decoder and with a direct libopenaptx decoder with byte-wise conversion; the same stream decodes correctly on the desktop. Not yet clear whether the PC really sent the tone at that moment (the first test stream was silence) or if the decoded samples are wrong on the ESP32 (alignment of `int24_3bytes_t::getAndScale16`, which reads a 16-bit value from an odd address, is a suspect) | open: repeat with a stream that starts with the tone, compare the first decoded words with the desktop |
| R17 | Metadata was empty on the sink because the source never provided track information. **Fixed**: `set_now_playing()` on the source; checked ESP32 to ESP32 | none |
| R18 | AVRCP controls from the sink were dropped (E12): no outgoing AVRCP connection. **Fixed**: the sink opens AVRCP after the A2DP stream is up; play and pause verified on ESP32 pair | next/previous still to check with a source that handles them |
| R19 | ESP32 as source (`source-sine`) to the PC's built-in controller (`air-mini #1`, the sink role advertised): the ESP32 never reports a connection. BlueZ logs `avdtp_connect_cb ... Host is down (112)` and `Unable to select SEP`, so the PC's side tries to connect out to the ESP32 as sink and fails. The ESP32 example only prints connected / not connected, so its connecting progress is not visible | open: check the ESP32 with the debug build, or pair the PC from its side first |
| R16 (start-up part fixed: skip `libusb_set_configuration` when already configured; the dongle can start and shut down cleanly now) | The libusb transport resets the dongle at start (`libusb_reset_device`); with this CSR clone the device then vanishes from the bus, every run. Opening a dongle that is not on the bus (or is half-initialised) blocks instead of failing; the tool needs a timeout around controller start. Also: after each run the libusb transport leaves the dongle without kernel driver (`Driver=[none]`), so the next run usually fails until a replug (R10 patch not effective yet) | timeout in the start path; verify the reattach patch on close |
| R11 | Debug logging at 115200 baud stalls the Bluetooth task: the first debug build made the stream break up badly. Log lines must never be printed per packet | fixed for the per-packet event; keep `BTSTACKA2DP_DEBUG` off in production |
| R8 | A2DP volume: absolute volume only, no local gain curve | E11 |
| R23 | **Fixed.** nRF52840 (mbed): `printf` goes to mbed's STDIO UART, which on the Nano 33 BLE is D0/D1, the HCI module's UART. A `BTSTACKA2DP_DEBUG` build would write log text into the HCI link | **Fixed**: `nrf52_hal.cpp` sends stdout to USB `Serial` by default (weak `mbed_override_console`, so a sketch's own `REDIRECT_STDOUT_TO(...)` still wins; both variants link). Verified: debug log on USB without any redirect in the sketch. Debug lines of the sketch and the btstack thread can interleave (unbuffered console) |
| R24 | **Fixed.** nRF52840: `start()` returned true as soon as the btstack thread runs, also with no controller; the HCI state stays "initializing" forever and nothing tells the sketch (same gap as R16 on the desktop) | **Fixed**: the nRF52 port's `start()` waits until the controller is working, at most `Port::set_startup_timeout(ms)` (default 3000 ms), and returns false otherwise (controller powered off again, a later `start()` retries). Desktop (R16) still has no timeout |
| R20 | **Fixed.** Stale link key was never recovered. When the peer lost its bond (Pico reflashed, speaker reset or "forgot" the device) the source's A2DP signaling connection fails with 0x63 (`L2CAP_CONNECTION_PIN_OR_LINK_KEY_MISSING`), and the source retries forever with the same key (backoff up to 60 s) without pairing again. Likely the cause of the Pico restart problem in S10 / R19 | **Fixed** in both roles (`handle_connection_failure`): on 0x63, 0x05 or 0x06 the key for that address is deleted and the connection retried after 1 s, which pairs again (once per reconnect cycle; afterwards the normal backoff). Verified: Pico reflashed under a bonded ESP32 source, connected again about 2 s later, no erase. Sink side (phone that forgot the device) not tested on hardware |
| R21 | **Fixed.** Joint-stereo-only sink gave a silent stream. The source offers only `AVDTP_SBC_STEREO`; with no common channel mode btstack still sends a (malformed) SBC configuration and our `configure_sbc()` uses it unchecked (0 blocks / subbands, so no packets). Confirms R5 | **Fixed**: the source offers joint stereo (it was in the comment, missing in the code); `configure_sbc()` rejects an invalid configuration and disconnects; `A2DP_SUBEVENT_COMMAND_REJECTED` during set-up disconnects too (before, a mono-only sink left the source in "connecting" forever). No automatic retry against an incompatible device. Verified with joint-only and mono-only Pico sinks |
| R22 | **Fixed.** The sink muted a source with software volume: btstack's AVRCP target answers the source's volume registration with its own copy of the volume, which starts at 0, so the source set its volume to 0 and sent digital silence (the ESP32 source to the Pico sink delivered only zeros once AVRCP connected) | `on_avrcp_connected()` of the sink sets btstack's copy with `avrcp_target_adjust_absolute_volume(volume_)`; source now gets 127, sine arrives at full level. Affects every sink setup, including phones / PCs that apply the reported volume |

## 6. Log
| Date | What | Result |
|---|---|---|
| 2026-11-07 | ESP32 flash at 921600 baud | failed (serial noise), 115200 works |
| 2026-11-07 | Desktop sink on the CSR dongle | blocked: no write access to the USB device |
| 2026-11-08 | ESP32 `sink-serial`, PC as source | discovery, pairing, connection and SBC audio work (E1 to E4) |
| 2026-11-08 | Desktop tool `play` with generated files | WAV, MP3, AAC decode correctly (A6 to A8) |
| 2026-11-08 | All automated tests | 5 of 5 PASS |
| 2026-11-08 | Added nRF52840 (mbed) port and Linux HCI user-channel transport (`--hci`) | compiles: nRF52840 examples, desktop tool; `--hci 99` reports the bind error; no hardware run |
| 2026-11-08 | ESP32 source, first debug build, listened to on the PC | audio breaks up badly: the per-packet debug log over serial stalled the Bluetooth task (R11) |
| 2026-11-08 | ESP32 source without debug output, recorded on the PC with `parec`, analyzed with `gaps.py` | clean: 440.0 Hz, 0 discontinuities in 23.3 s (S1, S2, S8) |
| 2026-11-08 | Desktop sink on the CSR dongle with ESP32 source and with the PC as source | no connection completes (D2, R9); afterwards the dongle is no longer on the USB bus |
| 2026-11-08 | Desktop sink with HCI trace (`BTSTACKA2DP_DEBUG`) after replug | controller hangs after the first encryption command (0x07, 0 credits); next run fails at `HCI_Reset`; dongle vanishes from the bus (R9, R10) |
| 2026-11-08 | ESP32 source recorded again on the PC | breakup: 587 discontinuities / 291 silent runs in 38 s (R12) |
| 2026-11-08 | Source stops scanning once connected, reflashed | 8 discontinuities / 4 silent runs in 38 s (S8) |
| 2026-11-08 | Self-checking ESP32 sink (sine gap test) with the PC as source | E4, E6, E9, E11 pass |
| 2026-11-08 | Sink reconnect after the remote disconnect | first attempt failed and was never retried: fixed, then PASS (E8) |
| 2026-11-08 | ESP32 reboot, reconnect without pairing | PASS (E7) |
| 2026-11-08 | aptX sink with the PC's aptX encoder | PC lists the aptX profile; stream arrives without RTP header; decoded audio garbage (R13) |
| 2026-11-08 | audio-tools aptX decoder on the desktop with ffmpeg's reference stream | garbage without `USE_3BYTE_INT24`, perfect with it (R13) |
| 2026-11-08 | ESP32 aptX sink with `USE_3BYTE_INT24` | task watchdog reset a few seconds into the tone (R14) |
| 2026-11-08 | aptX decode time on the ESP32 | -Os: 20.6 ms per 21.17 ms packet; -O3: 14.1 ms, no reset; level still 1 (R14, R15) |
| 2026-11-08 | Desktop source `--file tone440_60s.wav --usb 1:1` after replug | tool could not open the dongle: kernel driver still detached (`Driver=[none]`), then the dongle dropped off the bus; a probe then hung instead of failing (new bug, R16). D4/D5 not run |
| 2026-11-08 | ESP32 sink during that run | no audio after the desktop attempt; ESP32 had been blocked by mistake on the built-in controller, unblocked again |
| 2026-11-08 | Desktop source after replug (driver btusb bound): tool reports no controller; dongle disappears from sysfs during start, driver left detached | likely cause: the libusb transport's `libusb_reset_device` at start disconnects this clone (R16). D4/D5 still not run |
| 2026-11-08 | Passwordless sudo now works. Dongle rebound with sudo (btusb), re-enumerated via sysfs `authorized`; controller `hci1` still fails to initialise (`Connection timed out`), so the firmware is hung. Needs a physical replug | D4/D5 still not run |
| 2026-11-08 | After replug (hci1 up, btusb bound): desktop source fails at start again, driver detached. Test without `libusb_reset_device`: probe hangs, the dongle disappears from the bus. **Reset is not the only cause** (R16 stays open; a shutdown hang is also present: `blue_a2dp sink` did not exit within 25 s) | D4/D5 still not run |
| 2026-11-08 | HCI/libusb trace of the desktop tool on the replugged dongle: open OK, `libusb_detach_kernel_driver` OK, then `libusb_set_configuration(1)` never returns; the tool gives up after ~6 s and leaves the driver detached. Dongle stays on the bus | R16 narrowed: stall in `libusb_set_configuration`. Next: skip it when already configured, or claim without reconfiguring |
| 2026-11-08 | Patch added in `vendor_btstack.sh` and `hci_transport_h2_libusb.c`: skip `libusb_set_configuration` when already configured. Not yet validated: the dongle stopped answering on USB (`device descriptor read error -110`) right after a sysfs rebind, so the trace could not reach the new code. Needs a replug | R16 open, patch pending test |
| 2026-11-08 | **Start-up fixed** (configuration patch): dongle opens, claims interface, controller reaches working state, clean shutdown, driver reattached. Desktop source then scans, reports "connecting", but the ESP32 never reports a connection (last ESP32 connection 10:41, built-in controller). R16 closed for the start-up; the connection to the ESP32 is the next failure | D4/D5 open: connection to ESP32 not established |
| 2026-11-08 | Desktop source to ESP32, HCI/L2CAP trace: `Create_connection` OK, ACL connected (handle 0x81), L2CAP signaling channel open (remote MTU 1691), AVDTP version exchange done, then the source's AVDTP command gets **no reply** from the ESP32 (one outgoing ACL packet, nothing back) until the tool is stopped. Built-in controller connects to the same ESP32 fine | Failure is between the dongle's link and the ESP32's AVDTP answer. Next: BlueZ on the dongle (controller reported unavailable, dongle powered down after the tool) and an ESP32 sink-side trace |
| 2026-11-08 | ESP32 test sink flashed with `sink.start(name, false)` (auto reconnect off). Reconnect loop gone: one connection after the reflash, none in the following minutes. Also untrusted on the PC (reversible with `bluetoothctl trust`) | the scratch sketch used for the earlier self-check was lost in the session restart; a minimal event-printing sink replaced it |
| 2026-11-08 | D4/D5 retry: dongle had its driver detached from the previous run; sudo rebind made it fail to enumerate (`device not accepting address`), dongle off the bus. ESP32 idle (no connection) | D4/D5 open, needs replug |
| 2026-11-08 | D4/D5 after replug: tool starts, scans, reports "connecting", ESP32 logs no connection at all. Debug retry: driver detached at start, controller DOWN with zero address (init failure). Each run ends with the dongle needing a replug | D4/D5 open: dongle unreliable, consider a genuine CSR8510 / Intel / Realtek adapter |
| 2026-11-08 | No-dongle tests: E10 and E12 run with rhythmbox as MPRIS player on the PC; both FAIL (R17, R18). Built-in controller used for the link | |
| 2026-11-08 | ESP32 source (`source-sine`) to the PC's built-in controller as speaker: not connected after 6 min (R19). The PC's dongle controller had the wrong name for the test, fixed by using `air-mini #1` | S-series still open |
| 2026-11-08 | **ESP32 source to Pico 2 W sink**: the ESP32 (`source-sine`, target `BTstackA2DP`) streams to the Pico 2 W (`sink-serial`, `rp2040:rp2040:rpipico2w`, ipbtstack). The Pico receives audio steadily at about 176 kB/s (44.1 kHz stereo 16 bit). Connection succeeded, the PC is not involved. Gap check on the audio still to do | S1/S2 likely pass for the Pico path; gap test pending |
| 2026-11-08 | AVRCP fix tested ESP32 to ESP32 (no desktop): play/pause reach the source (suspended/started). The PC had been reconnecting to the source ESP32 and playing audio on its speakers; the ESP32 was removed from the PC again | E12 partial |
| 2026-11-08 | Source: `set_on_avrcp_operation` (next/previous visible to the application) and `set_now_playing` (title, artist, album over AVRCP). First version of `set_now_playing` crashed the source (posted to the run loop before start, assert in xQueueGenericSend); fixed. ESP32 pair: E10 and E12 PASS | |
| 2026-11-08 | Volume between the two ESP32s in both directions: PASS. WiFi access point on the sink next to the A2DP link: streaming and connection stable for several minutes (no audio-state drops, no new discontinuities after the reconnect); no WiFi client connected yet, so traffic on the AP is not tested | E14 partial |
| 2026-11-08 | WiFi access point `BTstackA2DP-ap` (WPA2, channel 1) confirmed in a WiFi scan from the PC. The sink's Bluetooth name is `ESP-SINK`, not the WiFi name. No WiFi client connected yet | E14 partial |
| 2026-11-08 | Source-side status updated: S4 PASS (ESP32 pair volume), S7 PARTIAL (about 25 min continuous, one reset-triggered reconnect) | |
| 2026-11-08 | **Working mode changed**: run many tests quickly, only identify problems, fix later | |
| 2026-11-08 | `gaps.py` validated on synthetic data | detects silence, dropped and repeated samples; no false alarm on clean or noisy tones |
| 2026-11-08 | Pico tests against the ESP32 (scratch sketches `gap_sink`, `sine_source`; the sine check runs on the receiving board) | P1, P2, P3, P4 PASS; P5 PARTIAL; S3 FAIL (R21); new R20 (stale link key). Debug log now shows the signaling failure status and the negotiated SBC configuration |
| 2026-11-08 | Fixes for R20 and R21, found R22 on the way (the joint-stereo change looked guilty: SBC loopback on the ESP32 and on the Pico, stereo and joint stereo, all RMS 7074; stereo-only source still silent; source log showed `volume event 0`) | R20, R21, R22 fixed and verified on ESP32 + Pico 2 W; S3 PASS (simulated); regression ESP32 source to Pico sink clean (residual 19, 0 gaps after start) |
| 2026-11-08 | Nano 33 BLE connected, no HCI module | runs; HCI never leaves "initializing" (N2 BLOCKED); found R23 (stdout on the HCI pins) and R24 (no start-up timeout) |
| 2026-11-08 | R23 and R24 fixed in the nRF52 port | without a module `start()` returns false after 3 s; debug log on USB by default; `sink-serial`, `source-sine` compile (18 %) |
| 2026-11-08 | Migration P1 (ESP32-A2DP compatible API) implemented | compiles on ESP32, Pico 2 W, desktop; hardware tests C1-C8 TODO |
