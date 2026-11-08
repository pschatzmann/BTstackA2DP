/**
 * A2DP source which sends a 440 Hz sine wave as aptX (falls back to SBC if the speaker
 * does not support aptX). The encoder comes from arduino-audio-tools / codec-openaptx. Change SPEAKER_NAME to your speaker (pairing mode).
 */
#include <math.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecAPTX.h"
#include "BTstackA2DP.h"
#include "BTstackA2DP/codecs/A2DPCodecAptxEncoder.h"

#define SPEAKER_NAME "MySpeaker"

BluetoothA2DPSource a2dp_source;
APTXEncoder aptx_encoder;
blue_a2dp::A2DPCodecAptxEncoder<APTXEncoder> aptx(aptx_encoder);
static float phase = 0.0f;

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
  a2dp_source.add_codec(&aptx);  // before start()
  a2dp_source.set_on_connection_state_changed(on_connection);
  a2dp_source.start(SPEAKER_NAME, get_audio);
}

void loop() { delay(1000); }
