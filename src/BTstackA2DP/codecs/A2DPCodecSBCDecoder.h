// Use the SBC decoder of audio-tools (arduino-libsbc) instead of the SBC decoder built into
// btstack:
//
//   #include "AudioTools.h"
//   #include "AudioTools/AudioCodecs/CodecSBC.h"
//   #include "BTstackA2DP.h"
//   #include "BTstackA2DP/codecs/A2DPCodecSBCDecoder.h"
//
//   audio_tools::SBCDecoder sbc_decoder;
//   blue_a2dp::A2DPCodecSBCDecoder<audio_tools::SBCDecoder> sbc(sbc_decoder);
//   sink.set_sbc_codec(&sbc);      // before sink.start()
//
// Unlike the other codecs this does not add a stream endpoint: it replaces the decoder of the
// built-in SBC endpoint.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"

namespace blue_a2dp {

template <class Decoder>
class A2DPCodecSBCDecoder : public SinkCodec, public Print {
 public:
  explicit A2DPCodecSBCDecoder(Decoder& decoder) : dec_(decoder) {}

  // not used: the built-in SBC endpoint is reused
  uint8_t codec_type() const override { return AVDTP_CODEC_SBC; }
  const uint8_t* capabilities(uint8_t& len) override {
    len = 0;
    return nullptr;
  }
  uint8_t* config_buffer(uint8_t& len) override {
    len = 0;
    return nullptr;
  }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION) return false;
    rate_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(e);
    channels_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(e);
    return true;
  }

  bool open(PcmOutput* out) override {
    if (opened_) return true;
    out_ = out;
    dec_.setOutput(*this);
    opened_ = dec_.begin();
    return opened_;
  }

  void decode(const uint8_t* payload, uint16_t len) override {
    // first byte: fragmentation flags and number of frames, then the SBC frames
    if (opened_ && len > 1) dec_.write(payload + 1, len - 1);
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
  uint32_t rate_ = 44100;
  int channels_ = 2;
  bool opened_ = false;
};

}  // namespace blue_a2dp
