# Sink (Bluetooth speaker)

Receives audio from a phone or PC, decodes it and hands 16 bit PCM to your code.

```cpp
#include "BTstackA2DP.h"
BluetoothA2DPSink sink;
void on_audio(const uint8_t* pcm, uint32_t len) { /* 16 bit interleaved PCM */ }
void setup() { sink.set_stream_reader(on_audio); sink.start("My Speaker"); }
void loop() {}
```

The callback runs in the Bluetooth task: keep it short. `examples/sink-i2s` decouples it with an
audio-tools `RingBufferSPSC` and plays through `I2SStream`.

## API
| Method | Purpose |
|---|---|
| `start(name, auto_reconnect)` / `end()` | start / stop the stack |
| `set_stream_reader(cb)` | receives the PCM data |
| `set_audio_info_callback(cb)` | sample rate and channels of the stream |
| `set_on_connection_state_changed(cb)`, `set_on_audio_state_changed(cb)` | state callbacks |
| `set_avrc_metadata_callback(cb)` | title, artist, album, ... (AVRCP) |
| `set_on_volume_change(cb)`, `set_volume(v)` | absolute volume 0..127 |
| `play()`, `pause()`, `stop()`, `next()`, `previous()` | control the player on the phone |
| `disconnect()`, `reconnect()`, `set_pin_code()`, `set_pairing_confirm_callback()` | connection handling |
| `add_codec()`, `set_sbc_codec()` | additional codecs, see [codecs.md](codecs.md) |
