/**
 * A2DP sink with AAC + SBC playing through an I2S DAC (PCM5102, MAX98357, ...) using the
 * arduino-audio-tools library (https://github.com/pschatzmann/arduino-audio-tools).
 *
 * The stream reader (Bluetooth task) only writes into a lock free queue; loop()
 * copies it to the I2S output, so the Bluetooth stack is never blocked.
 */
#include "AudioTools.h"
#include "AudioTools/Concurrency/LockFree.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"
#include "BlueA2DP.h"
#include "BlueA2DP/codecs/A2DPCodecAACDecoder.h"

BluetoothA2DPSink a2dp_sink;
// AAC decoding with libhelix (needs the codec-helix library). The phone will use AAC
// when it supports it (e.g. iPhones) and SBC otherwise.
AACDecoderHelix helix;
blue_a2dp::A2DPCodecAACDecoder<AACDecoderHelix> aac(helix);
I2SStream i2s;
RingBufferSPSC<uint8_t> buffer(20 * 1024);  // lock free: Bluetooth task -> loop()
QueueStream<uint8_t> queue(buffer);
StreamCopy copier(i2s, queue);

// Bluetooth task: store the PCM data (dropped if the queue is full)
void on_audio(const uint8_t* data, uint32_t len) {
  queue.write(data, len);
}

// the sample rate is only known once the stream starts
void on_info(const blue_a2dp::AudioInfo& info, void*) {
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate = info.sample_rate;
  cfg.channels = info.channels;
  cfg.bits_per_sample = 16;
  cfg.pin_bck = 26;
  cfg.pin_ws = 25;
  cfg.pin_data = 22;
  i2s.begin(cfg);
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  queue.begin(50);  // start playing when the queue is 50% filled
  a2dp_sink.add_codec(&aac);  // before start()
  a2dp_sink.set_audio_info_callback(on_info);
  a2dp_sink.set_stream_reader(on_audio);
  a2dp_sink.start("BlueA2DP Speaker");
}

void loop() {
  copier.copy();
}
