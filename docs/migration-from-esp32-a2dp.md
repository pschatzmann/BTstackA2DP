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
| Output | Audio-tools `Print` / `Stream` passed to the constructor | PCM callback (`set_stream_reader`) with a lock-free queue in the sketch |
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
| `BluetoothA2DPSink a2dp_sink(out);` (output stream in the constructor) | `BluetoothA2DPSink a2dp_sink;` + `set_stream_reader(cb)` | No stream constructor. See section 4.1 |
| `BluetoothA2DPSource a2dp_source;` | same | |
| `a2dp_sink.start("MyMusic");` | `a2dp_sink.start("MyMusic");` | Second argument is `auto_reconnect` (default `true`) |
| `a2dp_source.start("Speaker");` | `a2dp_source.start("Speaker", get_audio);` | The frame callback is passed to `start()` or set with `set_data_callback_in_frames()` |
| `a2dp_source.start();` (scan, choose in SSID callback) | not available | `start()` needs a name. See P1-4 |
| `set_data_callback_in_frames(cb)` | same | Frames of 16 bit stereo |
| `set_data_callback(cb)` | same | Raw bytes |
| `set_stream_reader(cb, also_output)` | same | `also_output` is accepted and ignored |
| `set_on_data_received(cb)` | same | |
| `set_avrc_metadata_callback(cb)` | same signature `(uint8_t id, const uint8_t* text)` | Attribute ids use the same values as `ESP_AVRC_MD_ATTR_*` |
| `set_avrc_metadata_attribute_mask(mask)` | not available | See P1-3 |
| `set_avrc_rn_playstatus_callback(cb)` | same, takes `blue_a2dp::PlayStatus` | |
| `set_avrc_rn_play_pos_callback(cb)` | not available | See P1-2 |
| `set_avrc_passthru_command_callback(cb)` | `set_on_avrcp_operation(cb, obj)` | Callback gets `(op, pressed, obj)` |
| `set_on_connection_state_changed(cb)` | same, takes `blue_a2dp::ConnectionState` | Enum values: `Disconnected`, `Connecting`, `Connected`, `Disconnecting` |
| `set_on_audio_state_changed(cb)` | same, takes `blue_a2dp::AudioState` | Values: `Suspended`, `Stopped`, `Started` |
| `set_ssid_callback(cb)` (source) | same, `bool cb(name, address, rssi)` | |
| `set_rssi_callback`, `set_rssi_active`, `get_last_rssi` | not available | See P1-1 |
| `set_auto_reconnect(bool)` | same | |
| `set_connected(bool)` | `disconnect()` / `reconnect()` | See P1-5 |
| `set_volume(v)` | same, 0..127 | |
| `set_default_bt_mode(...)` | N/A | Classic-only stack |
| `set_bitrate_callback(cb)` (AAC encoder) | not available | The bitrate is clamped to the peer's maximum. See P1-7 |
| `add_decoder(a2dp_sbc)` / `add_decoder(a2dp_aac)` (sink) | `add_codec(&codec)` | `A2DPCodec*` wrappers in `BTstackA2DP/codecs/` |
| `add_encoder(a2dp_sbc)` / `add_encoder(a2dp_aac)` (source) | `add_codec(&codec)` | |
| SBC from audio-tools | `set_sbc_codec(&codec)` | Replaces btstack's SBC, see `sink-sbc-audiotools` |
| `set_pin_code`, `pin_code()` | `set_pin_code(pin)` | No getter |
| `activate_pin_code(true)`, `confirm_pin_code()` | `set_pairing_confirm_callback(cb)` | Callback gets the numeric value and returns accept / reject. See P1-6 |
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
| `bt_music_receiver` | Receiver to an I2S DAC | `sink-i2s` (queue, I2S set up in the audio-info callback) | Covered, different structure |
| `bt_music_receiver_32bits` | 32-bit I2S output | none. SBC is decoded to 16 bit PCM; `AudioInfo::bits_per_sample` is always 16 | Missing (by design). Convert in the stream reader |
| `bt_music_receiver_and_BLE` | Title in a BLE characteristic while A2DP runs | none. No BLE in the build | N/A (BLE coexistence untested, see P3-2) |
| `bt_music_receiver_arduino_i2s` | Arduino `ESP_I2S` driver | none. The examples use audio-tools I2S only | Missing |
| `bt_music_receiver_arduino_i2s_3` | Arduino `I2S` driver, ESP32 core 3.x | none | Missing |
| `bt_music_receiver_audioboards` | AudioKit boards with `AudioBoardStream` | none | Missing |
| `bt_music_receiver_codec` | SBC and AAC decoders | `sink-aac-i2s` (`add_codec`), `sink-sbc-audiotools` (`set_sbc_codec`) | Covered |
| `bt_music_receiver_datacallback` | Raw data callback | `set_stream_reader` | Covered |
| `bt_music_receiver_disconnect-reconnect` | Manual disconnect and reconnect | `disconnect()` / `reconnect()`, no example | Partial |
| `bt_music_receiver_playing_status_callbacks` | Play status and play position | play status yes; play position missing | Partial |
| `bt_music_receiver_queued` | Queued receiver | `sink-i2s` uses the same idea | Covered |
| `bt_music_receiver_reconnect` | Auto reconnect | `start(name, true)` | Covered |
| `bt_music_receiver_resample48000` | Always 48 kHz output | none. The I2S clock follows the stream rate from the audio-info callback | Missing (see P2) |
| `bt_music_receiver_rssi` | RSSI of the link | btstack has `gap_read_rssi()`, not exposed | Missing |
| `bt_music_receiver_simple_with_pin` | PIN pairing with a button to confirm | `set_pin_code()` and `set_pairing_confirm_callback()` | Partial |
| `bt_music_receiver_status_callback` | Connection and audio state | `set_on_connection_state_changed`, `set_on_audio_state_changed` | Covered |
| `bt_music_receiver_to_internal_dac` | Built-in DAC of the original ESP32 | none | Missing |
| `bt_music_receiver_with_auto_shutdown` | Deep sleep after 5 min without data | `set_on_data_received()` and `get_connection_state()` work, no example | Partial |
| `bt_music_receiver_with_metadata` | Title, artist, album, playing time; play / pause | metadata callback and `play()` / `pause()` yes; attribute mask missing | Partial |

### 3.2 Sender (source)

| ESP32-A2DP example | What it shows | BTstackA2DP equivalent | Status |
|---|---|---|---|
| `bt_music_sender` | 2 channel sine to a speaker | `source-sine` | Covered |
| `bt_music_sender_autoreconnect` | Auto reconnect | `set_auto_reconnect()` (common class), no example | Covered |
| `bt_music_sender_avrc` | Speaker buttons | `set_on_avrcp_operation()`, no example | Partial |
| `bt_music_sender_codec` | SBC and AAC encoders, AAC bitrate callback | `source-aac`, `source-sbc-audiotools`; no bitrate callback | Partial |
| `bt_music_sender_disconnect` | `set_connected()` | `disconnect()` / `reconnect()` | Partial |
| `bt_music_sender_ssid_callback` | Scan for speakers, choose in the callback; `start()` without name | `set_ssid_callback()` yes; `start()` needs a name | Partial |
| `bt_music_sender_status_callback` | Connection state | `set_on_connection_state_changed()` | Covered |

## 4. Differences that matter when porting

### 4.1 The output is a callback, not a stream

ESP32-A2DP writes the PCM directly into an audio-tools stream: `BluetoothA2DPSink a2dp_sink(i2s);`.
BTstackA2DP calls `set_stream_reader(cb)` in the Bluetooth task, and the sketch has to move the data
out of the callback. The reason is that the callback must not block, so the examples put the data into
a lock-free queue and copy it to the I2S stream in `loop()`:

```cpp
// ESP32-A2DP
I2SStream out;
BluetoothA2DPSink a2dp_sink(out);

// BTstackA2DP (see examples/sink-i2s)
RingBufferSPSC<uint8_t> buffer(20 * 1024);
QueueStream<uint8_t> queue(buffer);
StreamCopy copier(i2s, queue);
void on_audio(const uint8_t* data, uint32_t len) { queue.write(data, len); }
void loop() { copier.copy(); }
```

The I2S configuration moves into the audio-info callback, because the sample rate is only known
once the stream starts (`set_audio_info_callback()`).

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

### 4.3 Starting a source needs a name

`BluetoothA2DPSource::start(name, cb)` connects to the speaker with that name. There is no
`start()` that scans and lets the callback choose (ESP32-A2DP `bt_music_sender_ssid_callback`).

### 4.4 Pairing

`set_pin_code()` sets the PIN for legacy pairing (default `"0000"`). For Secure Simple Pairing, a
numeric-comparison confirmation goes to `set_pairing_confirm_callback()`. The ESP32 functions
`activate_pin_code()` and `confirm_pin_code()` have no direct equivalent yet.

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

### P1: close the gaps that block ESP32-A2DP examples

1. **RSSI** (unlocks `bt_music_receiver_rssi`). Add `set_rssi_callback()`, `set_rssi_active()`
   and `get_last_rssi()`, using btstack's `gap_read_rssi()` on the ACL connection handle, polled
   from the run loop. Effort: small.
2. **Play position** (unlocks `bt_music_receiver_playing_status_callbacks`). btstack reports
   `AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED` in the AVRCP controller; forward it to a new
   `set_avrc_rn_play_pos_callback()`. Effort: small.
3. **Metadata attribute mask** (unlocks `bt_music_receiver_with_metadata`). Let the sketch choose
   the attributes requested from the peer. Needs a check of btstack's element attribute request
   before implementing. Effort: small.
4. **Source scan without a name** (unlocks `bt_music_sender_ssid_callback`). Add
   `start(FrameDataCallback)` that uses the existing inquiry code and lets `set_ssid_callback()`
   return `true` to pick a device. Effort: medium.
5. **`set_connected(bool)`** as an alias for `reconnect()` / `disconnect()`, to make
   `bt_music_sender_disconnect` and `bt_music_receiver_disconnect-reconnect` one-line ports. Effort:
   very small.
6. **Pairing names** `activate_pin_code()` and `confirm_pin_code()` as thin wrappers over the existing
   callbacks. Effort: very small.
7. **AAC bitrate callback** (`set_bitrate_callback()`), so the sketch can change the bitrate that
   the encoder uses. Effort: small.
8. **Optional output adapter** for audio-tools streams: a header that accepts an audio-tools
   `Print` / `Stream` and performs the queue step of section 4.1, so that
   `BluetoothA2DPSink a2dp(out)` works again. It must stay optional, so the core remains free of
   audio-tools. Effort: medium. This is the largest difference for people moving from ESP32-A2DP.

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
