# Migrating from ESP32-A2DP

This page compares BTstackA2DP with [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP), the
library whose examples many people start from. It goes through the ESP32-A2DP examples one by one,
lists the API differences, and proposes improvements. The goal is that a sketch from
`bt_music_*` can be ported with small changes, and that the gaps are visible.

Status used below:

* **Covered**: same behaviour, the API may be named differently.
* **Partial**: the main use works, part of the example is missing.
* **Missing**: no equivalent yet.
* **N/A**: does not apply to BTstackA2DP (for example, BLE or the ESP32 Bluedroid stack).

## 1. Summary

| | ESP32-A2DP | BTstackA2DP |
|---|---|---|
| Bluetooth stack | Bluedroid (ESP-IDF) | BlueKitchen btstack (vendored), Classic only |
| Platforms | ESP32 (classic Bluetooth variant) | ESP32, RP2040 / RP2350 (Pico W), nRF52840 (Arduino Nano 33 BLE, with external HCI module), Linux and macOS (USB dongle), Raspberry Pi (onboard, Linux) |
| Output | Audio-tools `Print` / `Stream` passed to the constructor | Same (`BluetoothA2DPSink a2dp_sink(i2s)` or `set_output()`), or a PCM callback (`set_stream_reader`) with a lock-free queue in the sketch |
| Codecs | SBC and AAC decoders / encoders from audio-tools | SBC built into btstack; AAC, aptX, MP3 as plug-ins (`add_codec`) |
| Audio-tools dependency | Required | Optional (only for the codec plug-ins and the `_i2s` / `_audiotools` examples) |
| BLE | Can run next to A2DP (Bluedroid) | Not available (no BLE in the build) |
| Bonding storage | NVS | ESP32: NVS; desktop: file; nRF52840: RAM only; RP2040: not checked |

Both libraries share the same shape: `BluetoothA2DPSink` / `BluetoothA2DPSource`, `start(name)`,
callbacks for connection and audio state, and `add_*` for codecs. Most sketches need changes in the
setup and in the output code, not in the logic.

## 2. API mapping

| ESP32-A2DP | BTstackA2DP | Notes |
|---|---|---|
| `BluetoothA2DPSink a2dp_sink(out);` (output stream in the constructor) | same, or `set_output(out)` | Any object with `write(const uint8_t*, size_t)`: Arduino `Print`, audio-tools streams. `begin()` is called by `start()`, `setAudioInfo()` when the rate is known. See section 4.1 |
| `BluetoothA2DPSource a2dp_source;` | same | |
| `a2dp_sink.start("MyMusic");` | `a2dp_sink.start("MyMusic");` | Second argument is `auto_reconnect` (default `true`) |
| `a2dp_source.start("Speaker");` | `a2dp_source.start("Speaker", get_audio);` | The frame callback is passed to `start()` or set with `set_data_callback_in_frames()` |
| `a2dp_source.start();` (scan, choose in SSID callback) | same, also `start(get_audio)` | Without an SSID callback the first audio device found is used |
| `set_data_callback_in_frames(cb)` | same | Frames of 16 bit stereo |
| `set_data_callback(cb)` | same | Raw bytes |
| `set_stream_reader(cb, also_output)` | same | `also_output` is accepted and ignored |
| `set_on_data_received(cb)` | same | |
| `set_avrc_metadata_callback(cb)` | same signature `(uint8_t id, const uint8_t* text)` | Attribute ids use the same values as `ESP_AVRC_MD_ATTR_*` |
| `set_avrc_metadata_attribute_mask(mask)` | same, with `blue_a2dp::MD_TITLE`, `MD_ARTIST`, ... (same values as `ESP_AVRC_MD_ATTR_*`) | Default: all attributes (`MD_ALL`) |
| `set_avrc_rn_playstatus_callback(cb)` | same, takes `blue_a2dp::PlayStatus` | |
| `set_avrc_rn_play_pos_callback(cb, notif_interval)` | same | Position in ms. The peer reports every second; the callback is called at most every `notif_interval` seconds (default 10) |
| `set_avrc_passthru_command_callback(cb)` | `set_on_avrcp_operation(cb, obj)` | Callback gets `(op, pressed, obj)` |
| `set_on_connection_state_changed(cb)` | same, takes `blue_a2dp::ConnectionState` | Enum values: `Disconnected`, `Connecting`, `Connected`, `Disconnecting` |
| `set_on_audio_state_changed(cb)` | same, takes `blue_a2dp::AudioState` | Values: `Suspended`, `Stopped`, `Started` |
| `set_ssid_callback(cb)` (source) | same, `bool cb(name, address, rssi)` | |
| `set_rssi_callback`, `set_rssi_active`, `get_last_rssi`, `update_rssi` | same; the parameter is `blue_a2dp::RSSIData&` (fields `bda`, `stat`, `rssi_delta` as in ESP-IDF) | Read when the connection is established and then every 5 s (`set_rssi_active(true, interval_ms)`) |
| `set_auto_reconnect(bool)` | same | |
| `set_connected(bool)` | same | `true` = `reconnect()`, `false` = `disconnect()` |
| `set_volume(v)` | same, 0..127 | |
| `set_default_bt_mode(...)` | N/A | Classic-only stack |
| `set_bitrate_callback(cb)` (AAC encoder) | same, on `A2DPCodecAACEncoder` | Gets the bitrate agreed with the peer (requested value lowered to the peer's maximum); with a callback the sketch sets the encoder's bitrate itself |
| `add_decoder(a2dp_sbc)` / `add_decoder(a2dp_aac)` (sink) | `add_codec(&codec)` | `A2DPCodec*` wrappers in `BTstackA2DP/codecs/` |
| `add_encoder(a2dp_sbc)` / `add_encoder(a2dp_aac)` (source) | `add_codec(&codec)` | |
| SBC from audio-tools | `set_sbc_codec(&codec)` | Replaces btstack's SBC, see `sink-sbc-audiotools` |
| `set_pin_code`, `pin_code()` | `set_pin_code(pin)` | No getter |
| `activate_pin_code(true)`, `confirm_pin_code()`, `confirm_pin_code(code)`, `pin_code()`, `debounce()` | same | The phone shows a number, `pin_code()` returns it until the pairing is answered. Alternative: `set_pairing_confirm_callback(cb)` (synchronous) |
| `is_connected()`, `get_connection_state()`, `get_audio_state()` | same | |
| `play()`, `pause()` | same | |
| `set_now_playing(...)` (source) | same | |
| `begin(...)` on `Serial`, `I2S` | `Serial.begin(...)`, audio-tools `I2SStream` if used | Not part of the Bluetooth API |

Audio information (sample rate, channels) is reported with `set_audio_info_callback(cb)`; the
`AudioInfo` struct has `sample_rate`, `channels` and `bits_per_sample`.

## 3. Example by example

The ESP32-A2DP examples are listed in the order of their folder names. The `legacy-api` folder
holds sketches for the old ESP32-A2DP API and is not mapped here: use the examples above instead.

### 3.1 Receiver (sink)

| ESP32-A2DP example | What it shows | BTstackA2DP equivalent | Status |
|---|---|---|---|
| `bt_music_receiver` | Receiver to an I2S DAC | `BluetoothA2DPSink a2dp_sink(i2s)` works unchanged; `sink-i2s` shows the queued variant | Covered |
| `bt_music_receiver_32bits` | 32-bit I2S output | none. SBC is decoded to 16 bit PCM; `AudioInfo::bits_per_sample` is always 16 | Missing (by design). Convert in the stream reader |
| `bt_music_receiver_and_BLE` | Title in a BLE characteristic while A2DP runs | none. No BLE in the build | N/A (BLE coexistence untested, see P3-2) |
| `bt_music_receiver_arduino_i2s` | Arduino `ESP_I2S` driver | none. The examples use audio-tools I2S only | Missing |
| `bt_music_receiver_arduino_i2s_3` | Arduino `I2S` driver, ESP32 core 3.x | none | Missing |
| `bt_music_receiver_audioboards` | AudioKit boards with `AudioBoardStream` | none | Missing |
| `bt_music_receiver_codec` | SBC and AAC decoders | `sink-aac-i2s` (`add_codec`), `sink-sbc-audiotools` (`set_sbc_codec`) | Covered |
| `bt_music_receiver_datacallback` | Raw data callback | `set_stream_reader` | Covered |
| `bt_music_receiver_disconnect-reconnect` | Manual disconnect and reconnect | `set_connected()`, `disconnect()` / `reconnect()` | Covered |
| `bt_music_receiver_playing_status_callbacks` | Play status and play position | `set_avrc_rn_playstatus_callback()`, `set_avrc_rn_play_pos_callback()`; no track-change callback | Partial |
| `bt_music_receiver_queued` | Queued receiver | `sink-i2s` uses the same idea | Covered |
| `bt_music_receiver_reconnect` | Auto reconnect | `start(name, true)` | Covered |
| `bt_music_receiver_resample48000` | Always 48 kHz output | none. The I2S clock follows the stream rate from the audio-info callback | Missing (see P2) |
| `bt_music_receiver_rssi` | RSSI of the link | `set_rssi_active()`, `set_rssi_callback()`, `get_last_rssi()` | Covered |
| `bt_music_receiver_simple_with_pin` | PIN pairing with a button to confirm | `activate_pin_code()`, `pin_code()`, `confirm_pin_code()`, `debounce()` (`touchRead()` is ESP32 only) | Covered |
| `bt_music_receiver_status_callback` | Connection and audio state | `set_on_connection_state_changed`, `set_on_audio_state_changed` | Covered |
| `bt_music_receiver_to_internal_dac` | Built-in DAC of the original ESP32 | none | Missing |
| `bt_music_receiver_with_auto_shutdown` | Deep sleep after 5 min without data | `set_on_data_received()` and `get_connection_state()` work, no example | Partial |
| `bt_music_receiver_with_metadata` | Title, artist, album, playing time; play / pause | metadata callback, `set_avrc_metadata_attribute_mask()`, `play()` / `pause()` | Covered |

### 3.2 Sender (source)

| ESP32-A2DP example | What it shows | BTstackA2DP equivalent | Status |
|---|---|---|---|
| `bt_music_sender` | 2 channel sine to a speaker | `source-sine` | Covered |
| `bt_music_sender_autoreconnect` | Auto reconnect | `set_auto_reconnect()` (common class), no example | Covered |
| `bt_music_sender_avrc` | Speaker buttons | `set_on_avrcp_operation()`, no example | Partial |
| `bt_music_sender_codec` | SBC and AAC encoders, AAC bitrate callback | `source-aac`, `source-sbc-audiotools`, `set_bitrate_callback()` | Covered |
| `bt_music_sender_disconnect` | `set_connected()` | `set_connected()` | Covered |
| `bt_music_sender_ssid_callback` | Scan for speakers, choose in the callback; `start()` without name | `set_ssid_callback()`, `start()` | Covered |
| `bt_music_sender_status_callback` | Connection state | `set_on_connection_state_changed()` | Covered |

## 4. Differences that matter when porting

### 4.1 Output: a stream, or a callback with a queue

As in ESP32-A2DP, the PCM can go straight into a stream given to the constructor (or to
`set_output()`). Any object with `write(const uint8_t*, size_t)` works: an Arduino `Print`, an
audio-tools `I2SStream` or `AudioStream`. `start()` calls its `begin()`, and audio-tools streams get
the sample rate through `setAudioInfo()` once the stream starts:

```cpp
// ESP32-A2DP and BTstackA2DP
I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);
```

The data is written from the Bluetooth task, as in ESP32-A2DP: a blocking I2S write paces the
stream, but anything slow in that path delays the Bluetooth stack. The alternative is
`set_stream_reader(cb)` with a lock-free queue that `loop()` copies to the output, so the Bluetooth
task never waits (see `examples/sink-i2s`):

```cpp
RingBufferSPSC<uint8_t> buffer(20 * 1024);
QueueStream<uint8_t> queue(buffer);
StreamCopy copier(i2s, queue);
void on_audio(const uint8_t* data, uint32_t len) { queue.write(data, len); }
void loop() { copier.copy(); }
```

With the callback, the I2S configuration moves into the audio-info callback, because the sample rate
is only known once the stream starts (`set_audio_info_callback()`).

### 4.2 Codecs are added to the object

ESP32-A2DP: `a2dp_sink.add_decoder(a2dp_aac);`. BTstackA2DP: the codec wrapper is a template over the
audio-tools class and is added with `add_codec()` before `start()`:

```cpp
#include "BTstackA2DP/codecs/A2DPCodecAACDecoder.h"
AACDecoderHelix helix;
blue_a2dp::A2DPCodecAACDecoder<AACDecoderHelix> aac(helix);
a2dp_sink.add_codec(&aac);   // before start()
```

SBC is always there. Use `set_sbc_codec()` only to replace btstack's SBC with the audio-tools one.

### 4.3 Choosing the speaker

`BluetoothA2DPSource::start(name, cb)` connects to the speaker with that name. `start()` and
`start(cb)` search without a name: `set_ssid_callback()` sees every audio device that is found
and returns `true` for the one to connect to; without a callback the first one is used (ESP32-A2DP
`bt_music_sender_ssid_callback`).

### 4.4 Pairing

`set_pin_code()` sets the PIN for legacy pairing (default `"0000"`). For Secure Simple Pairing
with confirmation by the user, use the ESP32-A2DP functions: `activate_pin_code(true)` before
`start()`, then `pin_code()` returns the number that the phone shows, and `confirm_pin_code()`
(or `confirm_pin_code(code)`, which rejects a wrong number) answers the request. A synchronous
alternative is `set_pairing_confirm_callback()`. Both make the device announce a display with
yes / no, so the phone shows the number; without them pairing is "just works".

### 4.5 Bonding storage

Pairing data is stored per platform: NVS on ESP32, a file in the home directory on the desktop
(`--bonding FILE`, `--no-bonding` for RAM only), and RAM only on the nRF52840 port. Pairing on the
nRF52840 is lost after a reset.

### 4.6 Platforms and BLE

ESP32-A2DP runs on ESP32 variants with Classic Bluetooth. BTstackA2DP also runs on the Pico W,
the nRF52840 with an external HCI module, Linux and macOS (USB dongle), and the Raspberry Pi through
its onboard controller. BLE is not part of BTstackA2DP. Sketches that rely on BLE next to A2DP need
another solution until P3-2 is done.

### 4.7 Sample rate and bit depth

The stream arrives at the rate the phone or PC negotiated, and the callback tells you the rate.
BTstackA2DP does not resample and does not offer 24 or 32 bit output. ESP32-A2DP examples such as
`bt_music_receiver_resample48000` and `bt_music_receiver_32bits` have no direct counterpart.

### 4.8 Watchdog

The ESP32-A2DP source examples call `delay(1)` inside the data callback to keep the watchdog
quiet. BTstackA2DP's source callback runs in the Bluetooth task, so the same advice applies: keep the
callback short and let it return.

### 4.9 Volume

Both libraries use absolute volume 0..127 and send it to the peer with `set_volume()`. Software
gain is available on the sink (`set_volume_control(true)`) and the source.

## 5. Proposed improvements

Ordered by how many ESP32-A2DP examples they unlock and how much work they are.

### P1: close the gaps that block ESP32-A2DP examples (done)

All items are implemented with the ESP32-A2DP names (see section 2). Compiled on ESP32,
Pico W / Pico 2 W, nRF52840 and the desktop; the behaviour on real devices is still to be tested
(see `docs/tests.md`, section 3).

1. **RSSI**: `set_rssi_active(active, interval_ms = 5000)`, `set_rssi_callback()`,
   `get_last_rssi()`, `update_rssi()`. HCI Read RSSI on the ACL link when the connection is
   established and then periodically; the value is the delta to the controller's golden range,
   like ESP-IDF's `rssi_delta`.
2. **Play position**: `set_avrc_rn_play_pos_callback(cb, notif_interval = 10)`. btstack asks the
   peer for a report every second and registers again automatically; the callback is throttled to
   `notif_interval` seconds, and called at once after a track change.
3. **Metadata attribute mask**: `set_avrc_metadata_attribute_mask(flags)`. With a subset, the
   attributes are requested with `avrcp_controller_get_element_attributes()`; attributes outside
   the mask are also filtered out, as some peers send all of them.
4. **Source scan without a name**: `start()` and `start(cb)`.
5. **`set_connected(bool)`**.
6. **Pairing**: `activate_pin_code()`, `confirm_pin_code()`, `confirm_pin_code(code)`,
   `pin_code()`, and `debounce()` used by the ESP32-A2DP PIN example.
7. **AAC bitrate callback**: `A2DPCodecAACEncoder::set_bitrate_callback()` and `bitrate()`.
8. **Output stream**: `BluetoothA2DPSink a2dp_sink(out)` and `set_output(out)` for any object with
   `write(const uint8_t*, size_t)`. Implemented with templates in the sink itself instead of a
   separate header, so the core still does not include audio-tools. Unlike the proposal it writes
   directly from the Bluetooth task, as ESP32-A2DP does; the queue stays the recommended way for
   heavy outputs.

### P2: examples that exist in ESP32-A2DP but not here

These need no change to the library:

* `sink-arduino-i2s`: the ESP32 core's I2S driver without audio-tools (`bt_music_receiver_arduino_i2s`).
* `sink-auto-shutdown`: deep sleep after idle time (`bt_music_receiver_with_auto_shutdown`).
* `sink-resample`: resample to a fixed rate with audio-tools `ResampleStream`
  (`bt_music_receiver_resample48000`).
* `sink-32bit`: convert to 32 bit output in the stream reader (`bt_music_receiver_32bits`).
* `sink-internal-dac`: the built-in DAC of the original ESP32 (`bt_music_receiver_to_internal_dac`).
* `sink-audioboard`: AudioKit boards with `AudioBoardStream` (`bt_music_receiver_audioboards`).
* `sink-disconnect-reconnect`, `sink-status`, `sink-metadata`, `sink-playstatus` (examples for
  features that exist already).
* `source-avrc` (speaker buttons), `source-disconnect`, `source-status`, `source-autoreconnect`.

After P1 is done, the RSSI, play-position, metadata and scan examples follow.

### P3: platform and documentation

1. **Pin defaults.** `sink-i2s` uses pins 26 / 25 / 22, the ESP32-A2DP examples use 14 / 15 / 22.
   Put the pins in one `#define` block at the top of each `_i2s` example, so they are easy to change.
2. **BLE coexistence on the ESP32.** Test a sketch that runs A2DP and BLE together on the original
   ESP32 and record the result. Until then, document it as not supported.
3. **nRF52840 bonding in flash.** The port keeps pairing in RAM only. A flash-backed store for the
   btstack TLV interface would avoid re-pairing after each reset. Needs a design for the flash area.
4. **Name mapping.** Provide the table of section 3 in the README, so that users can find the
   BTstackA2DP example for each ESP32-A2DP example.
5. **Examples for features that are not in the library yet** should be marked as such in the
   README, not left out.

## 6. Checklist for a port

1. Replace `BluetoothA2DPSink a2dp(out)` with `BluetoothA2DPSink a2dp` and add `set_stream_reader()`.
   Move the I2S setup into `set_audio_info_callback()`.
2. Replace `add_decoder` / `add_encoder` with `add_codec()`, and include the codec wrapper header.
3. For sources, pass the name and frame callback to `start()`. Remove any call to `start()` without
   a name.
4. Replace `set_connected()` with `disconnect()` / `reconnect()`.
5. Replace `set_avrc_passthru_command_callback()` with `set_on_avrcp_operation()`.
6. Remove `set_default_bt_mode()` and BLE code, or keep BLE in a separate sketch.
7. Check the output: 16 bit PCM, the sample rate from the audio-info callback, and the watchdog
   advice of section 4.8.
8. Remove features that are missing (RSSI, play position, PIN activation, attribute mask, bitrate
   callback) or wait for the P1 items.
