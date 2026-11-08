# Source (Bluetooth transmitter)

Pulls PCM from your code, encodes it and streams it to a speaker or headset.

```cpp
int32_t get_audio(Frame* frames, int32_t count) { /* fill frames */ return count; }
BluetoothA2DPSource source;
void setup() { source.start("MySpeaker", get_audio); }   // name of the remote device
```

The speaker (in pairing mode) is found by an inquiry and matched by name.

## API
| Method | Purpose |
|---|---|
| `start(name, cb)` | scan for the device `name` and connect; `cb` provides `Frame`s |
| `start_raw(name, cb)` | same, the callback delivers raw 16 bit stereo bytes |
| `start_with_address(addr, cb)` | connect directly to a known address |
| `set_ssid_callback(cb)` | decide for each discovered device whether to connect |
| `set_local_name(name)` | name under which this device is visible |
| `set_volume(v)`, `set_volume_control(bool)` | volume (applied to the PCM data by default) |
| `set_on_connection_state_changed(cb)` | state callbacks |
| `play()`, `pause()`, `stop()` | start / suspend / stop the stream |
| `add_codec()`, `set_sbc_codec()` | additional codecs, see [codecs.md](codecs.md) |
