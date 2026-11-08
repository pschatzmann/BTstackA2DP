// AAC sink codec for A2DP built on an audio-tools style decoder, e.g.
//
//   #include "AudioTools.h"
//   #include "AudioTools/AudioCodecs/CodecAACHelix.h"
//   #include "BlueA2DP.h"
//   #include "BlueA2DP/codecs/A2DPCodecAACDecoder.h"
//
//   audio_tools::AACDecoderHelix helix;
//   blue_a2dp::A2DPCodecAACDecoder<audio_tools::AACDecoderHelix> aac(helix);
//   sink.add_codec(&aac);      // before sink.start()
//
// The decoder must provide setOutput(Print&), begin(), end() and write(data, len) and
// accept ADTS frames (as AACDecoderHelix / AACDecoderFDK / AACDecoderFAAD do).
// Needs a lot of RAM (libhelix ~35 KB): not for tiny targets.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"
#include "AacUtil.h"

namespace blue_a2dp {

template <class Decoder>
class A2DPCodecAACDecoder : public SinkCodec, public Print {
 public:
  explicit A2DPCodecAACDecoder(Decoder& decoder) : dec_(decoder) {
    // MPEG-2 AAC LC + MPEG-4 AAC LC, 44.1 / 48 kHz, 1 or 2 channels, VBR, up to 320 kbit/s
    caps_[0] = 0xC0;
    caps_[1] = 0x01;
    caps_[2] = 0x8C;
    caps_[3] = 0x84;
    caps_[4] = 0xE2;
    caps_[5] = 0x00;
    memset(cfg_, 0, sizeof(cfg_));
  }

  uint8_t codec_type() const override { return AVDTP_CODEC_MPEG_2_4_AAC; }
  const uint8_t* capabilities(uint8_t& len) override {
    len = sizeof(caps_);
    return caps_;
  }
  uint8_t* config_buffer(uint8_t& len) override {
    len = sizeof(cfg_);
    return cfg_;
  }

  bool configure(const uint8_t* event) override {
    if (event[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CONFIGURATION) return false;
    if (a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_local_seid(event) != seid)
      return false;
    rate_ = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_sampling_frequency(event);
    channels_ = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_num_channels(event);
    sr_index_ = aac::sample_rate_index(rate_);
    return sr_index_ >= 0;
  }

  bool open(PcmOutput* out) override {
    if (opened_) return true;
    out_ = out;
    dec_.setOutput(*this);
    opened_ = dec_.begin();
    return opened_;
  }

  void decode(const uint8_t* payload, uint16_t len) override {
    if (!opened_ || sr_index_ < 0) return;
    const uint8_t* frame = nullptr;
    uint16_t n = aac::latm_frame(payload, len, &frame);
    if (n == 0 || n + 7 > sizeof(adts_)) return;
    aac::adts_header(adts_, n, sr_index_, channels_);
    memcpy(adts_ + 7, frame, n);
    dec_.write(adts_, n + 7);
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
  uint8_t caps_[6];
  uint8_t cfg_[6];
  uint8_t adts_[7 + 1700];
  uint32_t rate_ = 44100;
  int channels_ = 2;
  int sr_index_ = -1;
  bool opened_ = false;
};

}  // namespace blue_a2dp
