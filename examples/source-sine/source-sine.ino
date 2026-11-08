/**
 * A2DP source which sends a 440 Hz sine wave to a Bluetooth speaker or headset.
 * Change SPEAKER_NAME to the name of your device (put it into pairing mode).
 */
#include <math.h>
#include "BlueA2DP.h"

#define SPEAKER_NAME "MySpeaker"

BluetoothA2DPSource a2dp_source;
static float phase = 0.0f;

// called by the Bluetooth task to get more audio: fill 'count' frames
int32_t get_audio(Frame* frames, int32_t count) {
  const float step = 2.0f * (float)M_PI * 440.0f / 44100.0f;
  for (int i = 0; i < count; i++) {
    int16_t sample = (int16_t)(sinf(phase) * 8000.0f);
    frames[i].channel1 = sample;
    frames[i].channel2 = sample;
    phase += step;
    if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
  }
  return count;
}

void on_connection(blue_a2dp::ConnectionState state, void*) {
  Serial.println(state == blue_a2dp::ConnectionState::Connected ? "connected" : "not connected");
}

void setup() {
  Serial.begin(115200);
  a2dp_source.set_on_connection_state_changed(on_connection);
  a2dp_source.set_volume(60);
  if (!a2dp_source.start(SPEAKER_NAME, get_audio)) {
    Serial.println("Bluetooth start failed");
  }
}

void loop() {
  delay(1000);
}
