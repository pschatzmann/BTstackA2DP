# Migrating from ESP32-A2DP

This page is for people who port a sketch from [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)
to BTstackA2DP. It lists only what is different or still missing; everything else uses the same
names and works the same way.

## 1. Summary

| | ESP32-A2DP | BTstackA2DP |
|---|---|---|
| Bluetooth stack | Bluedroid (ESP-IDF) | BlueKitchen btstack (vendored), Classic only |
| Platforms | ESP32 (classic Bluetooth variant) | ESP32, RP2040 / RP2350 (Pico W), nRF52840 (Arduino Nano 33 BLE, with external HCI module), Linux and macOS (USB dongle), Raspberry Pi (onboard, Linux) |
| Output | Audio-tools `Print` / `Stream` passed to the constructor | Same, or a PCM callback (`set_stream_reader`) with a queue (section 3.1) |
| Codecs | SBC and AAC decoders / encoders from audio-tools | SBC built into btstack; AAC, aptX, MP3 as plug-ins (`add_codec`) |
| Audio-tools dependency | Required | Optional (only for the codec plug-ins and audio-tools outputs) |
| BLE | Can run next to A2DP (Bluedroid) | Not available |
| Bonding storage | NVS | ESP32: NVS; desktop: file; nRF52840: RAM only; RP2040: not checked |

## 2. API differences

These work as in ESP32-A2DP and need no change: the sink and source classes, `start(name)`,
`start(name, auto_reconnect)`, a sink with an output stream (`BluetoothA2DPSink a2dp_sink(i2s)`,
`set_output()`), source `start()` with and without a name, `set_data_callback_in_frames`,
`set_data_callback`, `set_stream_reader`, `set_on_data_received`, `set_avrc_metadata_callback`,
`set_avrc_metadata_attribute_mask`, `set_avrc_rn_play_pos_callback`, `set_ssid_callback`,
`set_rssi_active` / `set_rssi_callback` / `get_last_rssi` / `update_rssi`, `set_auto_reconnect`,
`set_connected`, `set_volume`, `activate_pin_code` / `confirm_pin_code` / `pin_code` / `debounce`,
`is_connected`, `get_connection_state`, `get_audio_state`, `play`, `pause`, `set_now_playing`,
and the AAC encoder's `set_bitrate_callback`.

Different:

| ESP32-A2DP | BTstackA2DP | Notes |
|---|---|---|
| `a2dp_source.start("Speaker");` + separate data callback | `a2dp_source.start("Speaker", get_audio);` | Or set the callback with `set_data_callback_in_frames()` first |
| `start(std::vector<const char*> names)` (source) | not available | Use `set_ssid_callback()` and compare the names there |
| Enums `esp_a2d_connection_state_t`, `esp_a2d_audio_state_t`, `esp_avrc_playback_stat_t` | `blue_a2dp::ConnectionState`, `AudioState`, `PlayStatus` | Values: `Disconnected`, `Connecting`, `Connected`, `Disconnecting`; `Suspended`, `Stopped`, `Started`; `Stopped`, `Playing`, `Paused`, `FwdSeek`, `RevSeek`, `Error` |
| `ESP_AVRC_MD_ATTR_*` | `blue_a2dp::MD_TITLE`, `MD_ARTIST`, ... (`MD_ALL`) | Same values |
| `esp_bt_gap_cb_param_t::read_rssi_delta_param&` (RSSI callback) | `blue_a2dp::RSSIData&` | Same field names: `bda`, `stat`, `rssi_delta` |
| `esp_bd_addr_t` (SSID callback) | `const uint8_t*` | 6 bytes |
| `set_avrc_rn_track_change_callback(cb)` | not available | Use the metadata callback: it is called again on a track change |
| `set_avrc_passthru_command_callback(cb)` | `set_on_avrcp_operation(cb, obj)` | Callback gets `(op, pressed, obj)` |
| `add_decoder(a2dp_aac)` (sink) / `add_encoder(a2dp_aac)` (source) | `add_codec(&codec)` | `A2DPCodec*` wrappers in `BTstackA2DP/codecs/`, see section 3.2 |
| SBC from audio-tools | `set_sbc_codec(&codec)` | Replaces btstack's SBC, see `sink-sbc-audiotools` |
| `set_pin_code(pin, pin_type)` (source) | `set_pin_code(pin)` (sink and source) | Legacy PIN as a string, no PIN type; `pin_code()` returns the SSP number (section 3.4) |
| `set_default_bt_mode(...)` | N/A | Classic-only stack |
| `set_output_active()`, `set_output_active_by_state()` | not available | |

Audio information (sample rate, channels) comes from `set_audio_info_callback(cb)`
(`AudioInfo` with `sample_rate`, `channels`, `bits_per_sample`); an audio-tools output stream
gets it automatically through `setAudioInfo()`.

## 3. Differences that matter when porting

### 3.1 Output: stream or queue

`BluetoothA2DPSink a2dp_sink(out)` accepts any object with `write(const uint8_t*, size_t)`: an
Arduino `Print` or an audio-tools stream. `start()` calls its `begin()`, and audio-tools streams get
the sample rate once the stream starts. As in ESP32-A2DP, the data is written from the Bluetooth
task: a blocking I2S write paces the stream, but anything slow in that path delays the Bluetooth
stack.

For heavy outputs use `set_stream_reader(cb)` with a lock-free queue that `loop()` copies to the
output, so the Bluetooth task never waits (see `examples/sink-i2s`):

```cpp
RingBufferSPSC<uint8_t> buffer(20 * 1024);
QueueStream<uint8_t> queue(buffer);
StreamCopy copier(i2s, queue);
void on_audio(const uint8_t* data, uint32_t len) { queue.write(data, len); }
void loop() { copier.copy(); }
```

With the callback, the I2S configuration goes into the audio-info callback, because the sample rate
is only known once the stream starts (`set_audio_info_callback()`).

### 3.2 Codecs

The codec wrapper is a template over the audio-tools class and is added with `add_codec()` before
`start()`:

```cpp
#include "BTstackA2DP/codecs/A2DPCodecAACDecoder.h"
AACDecoderHelix helix;
blue_a2dp::A2DPCodecAACDecoder<AACDecoderHelix> aac(helix);
a2dp_sink.add_codec(&aac);   // before start()
```

SBC is always there. Use `set_sbc_codec()` only to replace btstack's SBC with the audio-tools one.

### 3.3 Sample rate and bit depth

The stream arrives at the rate the phone or PC negotiated. BTstackA2DP does not resample and
delivers 16 bit PCM only; `bt_music_receiver_resample48000` and `bt_music_receiver_32bits` need the
conversion in the sketch (audio-tools `ResampleStream`, or a conversion in the stream reader).

### 3.4 Pairing

`activate_pin_code(true)` before `start()` makes the phone show a number; `pin_code()` returns it
until the request is answered with `confirm_pin_code()` (or `confirm_pin_code(code)`, which rejects
a wrong number). `set_pairing_confirm_callback()` is a synchronous alternative. Without either,
pairing is "just works". `set_pin_code()` only applies to legacy pairing with old devices.

### 3.5 Bonding storage

NVS on ESP32, a file in the home directory on the desktop (`--bonding FILE`, `--no-bonding` for RAM
only), RAM only on the nRF52840: pairing on the nRF52840 is lost after a reset.

### 3.6 Platforms and BLE

BLE is not part of BTstackA2DP. Sketches that use BLE next to A2DP need another solution.

### 3.7 Watchdog

The source data callback runs in the Bluetooth task: keep it short and let it return (the
ESP32-A2DP examples call `delay(1)` there for the same reason).

## 4. ESP32-A2DP examples with open gaps

All other `bt_music_*` examples port with the API above. The `legacy-api` folder is not covered.

| ESP32-A2DP example | Gap |
|---|---|
| `bt_music_receiver_32bits` | 16 bit PCM only: convert in the stream reader (by design) |
| `bt_music_receiver_and_BLE` | No BLE |
| `bt_music_receiver_arduino_i2s`, `bt_music_receiver_arduino_i2s_3` | No example with the ESP32 core's I2S driver (should work through `set_output()`, not tested) |
| `bt_music_receiver_audioboards` | No example with `AudioBoardStream` (should work through `set_output()`, not tested) |
| `bt_music_receiver_playing_status_callbacks` | No track-change callback |
| `bt_music_receiver_resample48000` | No resampling in the library: use audio-tools `ResampleStream` |
| `bt_music_receiver_to_internal_dac` | No example for the original ESP32's built-in DAC |
| `bt_music_receiver_with_auto_shutdown` | Works with `set_on_data_received()` and `get_connection_state()`, no example |
| `bt_music_sender_avrc` | Works with `set_on_avrcp_operation()`, no example |

The functions added for ESP32-A2DP compatibility (RSSI, play position, metadata mask, source
`start()` without a name, `set_connected`, PIN confirmation, AAC bitrate callback, output stream)
compile on all platforms; their test on real devices is tracked in `docs/tests.md` (section 3.7).

## 5. Open improvements

### Examples

* `sink-arduino-i2s`: the ESP32 core's I2S driver without audio-tools (`bt_music_receiver_arduino_i2s`).
* `sink-auto-shutdown`: deep sleep after idle time (`bt_music_receiver_with_auto_shutdown`).
* `sink-resample`: resample to a fixed rate with audio-tools `ResampleStream`
  (`bt_music_receiver_resample48000`).
* `sink-32bit`: convert to 32 bit output in the stream reader (`bt_music_receiver_32bits`).
* `sink-internal-dac`: the built-in DAC of the original ESP32 (`bt_music_receiver_to_internal_dac`).
* `sink-audioboard`: AudioKit boards with `AudioBoardStream` (`bt_music_receiver_audioboards`).
* Examples for the compatible functions: `sink-rssi`, `sink-pin`, `sink-metadata` (with play
  status and play position), `sink-disconnect-reconnect`, `source-scan` (SSID callback),
  `source-avrc` (speaker buttons), `source-disconnect`.

### Library

1. **Track-change callback** (`set_avrc_rn_track_change_callback`), the last missing part of
   `bt_music_receiver_playing_status_callbacks`.
2. **Pin defaults.** `sink-i2s` uses pins 26 / 25 / 22, the ESP32-A2DP examples use 14 / 15 / 22.
   Put the pins in one `#define` block at the top of each `_i2s` example.
3. **BLE coexistence on the ESP32.** Test A2DP and BLE together on the original ESP32 and record
   the result. Until then it is not supported.
4. **nRF52840 bonding in flash.** A flash-backed store for btstack's TLV interface would avoid
   re-pairing after each reset. Needs a design for the flash area.
5. **Name mapping in the README**: the BTstackA2DP example for each ESP32-A2DP example.

## 6. Checklist for a port

1. Replace `add_decoder` / `add_encoder` with `add_codec()`, and include the codec wrapper header.
2. For sources, pass the frame callback to `start()` or set it with `set_data_callback_in_frames()`.
3. Replace the `esp_*` enums and types with the `blue_a2dp::` ones (section 2).
4. Replace `set_avrc_passthru_command_callback()` with `set_on_avrcp_operation()`.
5. Remove `set_default_bt_mode()` and BLE code, or keep BLE in a separate sketch.
6. If the output does heavy work, move it out of the Bluetooth task with `set_stream_reader()` and a
   queue (section 3.1).
7. Check the output: 16 bit PCM at the stream's sample rate, and the watchdog advice (section 3.7).
