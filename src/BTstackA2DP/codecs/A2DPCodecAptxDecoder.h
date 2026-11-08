// aptX sink codec for A2DP built on the audio-tools APTXDecoder (codec-openaptx):
//
//   #include "AudioTools.h"
//   #include "AudioTools/AudioCodecs/CodecAPTX.h"
//   #include "BTstackA2DP.h"
//   #include "BTstackA2DP/codecs/A2DPCodecAptxDecoder.h"
//
//   audio_tools::APTXDecoder dec;
//   blue_a2dp::A2DPCodecAptxDecoder<audio_tools::APTXDecoder> aptx(dec);
//   sink.add_codec(&aptx);      // before sink.start()
//
// aptX (4:1, 16 bit, 44.1 / 48 kHz) is a proprietary codec of Qualcomm / APT Ltd; the open
// implementation is only used for evaluation and personal projects: check the licence.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"
#include "AptxUtil.h"

namespace blue_a2dp {

template <class Decoder>
class A2DPCodecAptxDecoder : public SinkCodec, public Print {
 public:
  explicit A2DPCodecAptxDecoder(Decoder& decoder) : dec_(decoder) {
    aptx::make_info(caps_, aptx::kFreq44100 | aptx::kFreq48000 | aptx::kStereo);
    memset(cfg_, 0, sizeof(cfg_));
  }

  uint8_t codec_type() const override { return AVDTP_CODEC_NON_A2DP; }
  const uint8_t* capabilities(uint8_t& len) override {
    len = sizeof(caps_);
    return caps_;
  }
  uint8_t* config_buffer(uint8_t& len) override {
    len = sizeof(cfg_);
    return cfg_;
  }
  /// aptX packets carry the encoded data without RTP header
  bool uses_rtp() const override { return false; }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION) return false;
    if (a2dp_subevent_signaling_media_codec_other_configuration_get_local_seid(e) != seid) return false;
    const uint8_t* info = a2dp_subevent_signaling_media_codec_other_configuration_get_media_codec_information(e);
    uint8_t len = a2dp_subevent_signaling_media_codec_other_configuration_get_media_codec_information_len(e);
    if (!aptx::is_aptx(info, len)) return false;
    rate_ = aptx::sample_rate(info[6]);
    channels_ = aptx::channels(info[6]);
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
    if (opened_) dec_.write(payload, len);
  }

  void close() override {
    if (opened_) dec_.end();
    opened_ = false;
  }

  // Print: PCM from the decoder (16 bit)
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t* data, size_t len) override {
    if (out_ && channels_ > 0)
      out_->on_pcm((int16_t*)data, (int)(len / (2 * channels_)), channels_, (int)rate_);
    return len;
  }

 private:
  Decoder& dec_;
  PcmOutput* out_ = nullptr;
  uint8_t caps_[aptx::kInfoLen];
  uint8_t cfg_[aptx::kInfoLen];
  uint32_t rate_ = 44100;
  int channels_ = 2;
  bool opened_ = false;
};

}  // namespace blue_a2dp
