// aptX source codec for A2DP built on the audio-tools APTXEncoder (codec-openaptx):
//
//   audio_tools::APTXEncoder enc;
//   blue_a2dp::A2DPCodecAptxEncoder<audio_tools::APTXEncoder> aptx(enc);
//   source.add_codec(&aptx);      // before source.start()
//
// Used when the remote device supports aptX, otherwise SBC. See A2DPCodecAptxDecoder.h for the
// licence note.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"
#include "AptxUtil.h"

namespace blue_a2dp {

template <class Encoder>
class A2DPCodecAptxEncoder : public SourceCodec, public Print {
 public:
  explicit A2DPCodecAptxEncoder(Encoder& encoder) : enc_(encoder) {
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
  bool uses_rtp() const override { return false; }

  bool select(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CAPABILITY) return false;
    const uint8_t* info = a2dp_subevent_signaling_media_codec_other_capability_get_media_codec_information(e);
    uint8_t len = a2dp_subevent_signaling_media_codec_other_capability_get_media_codec_information_len(e);
    if (!aptx::is_aptx(info, len)) return false;
    uint8_t choice = aptx::choose(info[6]);
    if (choice == 0) return false;
    uint8_t cfg[aptx::kInfoLen];
    aptx::make_info(cfg, choice);
    return a2dp_source_set_config_other(
               a2dp_subevent_signaling_media_codec_other_capability_get_a2dp_cid(e), seid,
               a2dp_subevent_signaling_media_codec_other_capability_get_remote_seid(e), cfg,
               sizeof(cfg)) == ERROR_CODE_SUCCESS;
  }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION) return false;
    if (a2dp_subevent_signaling_media_codec_other_configuration_get_local_seid(e) != seid) return false;
    const uint8_t* info = a2dp_subevent_signaling_media_codec_other_configuration_get_media_codec_information(e);
    uint8_t len = a2dp_subevent_signaling_media_codec_other_configuration_get_media_codec_information_len(e);
    if (!aptx::is_aptx(info, len)) return false;
    rate_ = aptx::sample_rate(info[6]);
    return rate_ != 0;
  }

  uint32_t sample_rate() const override { return rate_; }
  /// 512 frames (~11 ms) -> ~512 bytes per packet: at least one packet per 10 ms timer tick
  uint16_t frames_per_packet() const override { return 512; }

  bool open() override {
    if (opened_) return true;
    out_len_ = 0;
    auto info = enc_.audioInfo();
    info.sample_rate = rate_;
    info.channels = 2;
    info.bits_per_sample = 16;
    enc_.setAudioInfo(info);
    enc_.setOutput(*this);
    opened_ = enc_.begin();
    return opened_;
  }

  void close() override {
    if (opened_) enc_.end();
    opened_ = false;
  }

  uint16_t encode(const int16_t* pcm, uint8_t* out, uint16_t max_len) override {
    if (!opened_) return 0;
    out_len_ = 0;
    enc_.write((const uint8_t*)pcm, frames_per_packet() * 2 * sizeof(int16_t));
    // the encoder delivers its output in bursts: send whatever was produced (multiple of 4)
    uint16_t n = out_len_ & ~3;
    if (n > max_len) n = max_len & ~3;
    memcpy(out, buf_, n);
    return n;
  }

  // Print: output of the encoder
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (out_len_ + len > sizeof(buf_)) len = sizeof(buf_) - out_len_;
    memcpy(buf_ + out_len_, data, len);
    out_len_ += len;
    return len;
  }

 private:
  Encoder& enc_;
  uint32_t rate_ = 44100;
  uint8_t caps_[aptx::kInfoLen];
  uint8_t cfg_[aptx::kInfoLen];
  uint8_t buf_[1024];
  uint16_t out_len_ = 0;
  bool opened_ = false;
};

}  // namespace blue_a2dp
