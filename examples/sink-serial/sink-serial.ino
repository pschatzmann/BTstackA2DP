/**
 * A2DP sink which just counts the received PCM data and prints the AVRCP metadata.
 * Pair your phone with "BTstackA2DP" and start playing music.
 *
 * The stream reader receives interleaved 16 bit PCM samples. Replace it with code
 * which writes to an I2S DAC (see sink-i2s).
 */
#include "BTstackA2DP.h"

BluetoothA2DPSink a2dp_sink;
volatile uint32_t received_bytes = 0;

// runs in the Bluetooth task: keep it short
void on_audio(const uint8_t* data, uint32_t length) {
  received_bytes += length;
}

void on_metadata(uint8_t id, const uint8_t* text) {
  Serial.printf("metadata 0x%02x: %s\n", id, (const char*)text);
}

void on_connection(blue_a2dp::ConnectionState state, void*) {
  Serial.println(state == blue_a2dp::ConnectionState::Connected ? "connected" : "not connected");
}

void on_audio_info(const blue_a2dp::AudioInfo& info, void*) {
  Serial.printf("audio: %lu Hz, %u channels\n", (unsigned long)info.sample_rate, info.channels);
}

void setup() {
  Serial.begin(115200);
  a2dp_sink.set_stream_reader(on_audio);
  a2dp_sink.set_avrc_metadata_callback(on_metadata);
  a2dp_sink.set_on_connection_state_changed(on_connection);
  a2dp_sink.set_audio_info_callback(on_audio_info);
  if (!a2dp_sink.start("BTstackA2DP")) {
    Serial.println("Bluetooth start failed");
  }
}

void loop() {
  Serial.printf("received %lu bytes\n", (unsigned long)received_bytes);
  delay(2000);
}
