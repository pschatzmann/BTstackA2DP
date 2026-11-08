// EXPERIMENTAL: MP3 (MPEG-1,2 Audio) sink codec for A2DP built on an audio-tools decoder:
//
//   audio_tools::MP3DecoderHelix mp3_decoder;
//   blue_a2dp::A2DPCodecMP3Decoder<audio_tools::MP3DecoderHelix> mp3(mp3_decoder);
//   sink.add_codec(&mp3);      // before sink.start()
//
// Very few phones offer MP3 over A2DP, so it will rarely be chosen over SBC / AAC.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"
#include "Mp3Util.h"

namespace blue_a2dp {

template <class Decoder>
class A2DPCodecMP3Decoder : public SinkCodec, public Print {
 public:
  explicit A2DPCodecMP3Decoder(Decoder& decoder) : dec_(decoder) {
    // layer III, 4 channel modes, CRC off, 44.1 + 48 kHz, bit rate index 0..12
    caps_[0] = 0x2F;
    caps_[1] = 0x03;
    caps_[2] = 0x1F;
    caps_[3] = 0xFF;
    memset(cfg_, 0, sizeof(cfg_));
  }

  uint8_t codec_type() const override { return AVDTP_CODEC_MPEG_1_2_AUDIO; }
  const uint8_t* capabilities(uint8_t& len) override {
    len = sizeof(caps_);
    return caps_;
  }
  uint8_t* config_buffer(uint8_t& len) override {
    len = sizeof(cfg_);
    return cfg_;
  }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AUDIO_CONFIGURATION) return false;
    if (a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_local_seid(e) != seid)
      return false;
    rate_ = a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_sampling_frequency(e);
    channels_ = a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_num_channels(e);
    return rate_ != 0;
  }

  bool open(PcmOutput* out) override {
    if (opened_) return true;
    out_ = out;
    dec_.setOutput(*this);
    opened_ = dec_.begin();
    return opened_;
  }

  void decode(const uint8_t* payload, uint16_t len) override {
    if (opened_ && len > mp3::kPayloadHeaderLen)
      dec_.write(payload + mp3::kPayloadHeaderLen, len - mp3::kPayloadHeaderLen);
  }

  void close() override {
    if (opened_) dec_.end();
    opened_ = false;
  }

  // Print: PCM from the decoder
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t* data, size_t len) override {
    if (out_ && channels_ > 0)
      out_->on_pcm((int16_t*)data, (int)(len / (2 * channels_)), channels_, (int)rate_);
    return len;
  }

 private:
  Decoder& dec_;
  PcmOutput* out_ = nullptr;
  uint8_t caps_[4];
  uint8_t cfg_[4];
  uint32_t rate_ = 44100;
  int channels_ = 2;
  bool opened_ = false;
};

}  // namespace blue_a2dp
