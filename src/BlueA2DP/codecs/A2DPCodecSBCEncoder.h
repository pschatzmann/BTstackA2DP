// Use the SBC encoder of audio-tools (arduino-libsbc) instead of the SBC encoder built into
// btstack:
//
//   audio_tools::SBCEncoder sbc_encoder;
//   blue_a2dp::A2DPCodecSBCEncoder<audio_tools::SBCEncoder> sbc(sbc_encoder);
//   source.set_sbc_codec(&sbc);      // before source.start()
//
// It replaces the encoder of the built-in SBC endpoint; the stream parameters (sample rate,
// blocks, subbands, allocation method, bitpool) are the ones negotiated with the speaker.
// The audio-tools encoder only does plain stereo (no joint stereo), which is what the source
// advertises.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"

namespace blue_a2dp {

template <class Encoder>
class A2DPCodecSBCEncoder : public SourceCodec, public Print {
 public:
  explicit A2DPCodecSBCEncoder(Encoder& encoder, int frames_per_media_packet = 4)
      : enc_(encoder), frames_per_media_packet_(frames_per_media_packet) {}

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
  bool select(const uint8_t*) override { return false; }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION) return false;
    rate_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(e);
    blocks_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(e);
    subbands_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(e);
    bitpool_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(e);
    if (bitpool_ > 53) bitpool_ = 53;  // keeps the packets small
    // Bluetooth definition (1 = loudness, 2 = SNR) -> SBC_AM_LOUDNESS / SBC_AM_SNR
    allocation_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(e) == 2 ? 1 : 0;
    return rate_ != 0;
  }

  uint32_t sample_rate() const override { return rate_; }
  uint16_t frames_per_packet() const override { return blocks_ * subbands_ * frames_per_media_packet_; }

  bool open() override {
    if (opened_) return true;
    auto info = enc_.audioInfo();
    info.sample_rate = rate_;
    info.channels = 2;
    info.bits_per_sample = 16;
    enc_.setAudioInfo(info);
    enc_.setSubbands(subbands_);
    enc_.setBlocks(blocks_);
    enc_.setBitpool(bitpool_);
    enc_.setAllocationMethod(allocation_);
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
    frames_ = 0;
    enc_.write((const uint8_t*)pcm, frames_per_packet() * 2 * sizeof(int16_t));
    if (frames_ == 0 || (uint32_t)out_len_ + 1 > max_len) return 0;
    out[0] = frames_;  // SBC payload header: no fragmentation, number of frames
    memcpy(out + 1, buf_, out_len_);
    return out_len_ + 1;
  }

  // Print: one SBC frame per call
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t* data, size_t len) override {
    if (out_len_ + len > sizeof(buf_)) return len;
    memcpy(buf_ + out_len_, data, len);
    out_len_ += len;
    frames_++;
    return len;
  }

 private:
  Encoder& enc_;
  int frames_per_media_packet_;
  uint32_t rate_ = 44100;
  uint8_t blocks_ = 16, subbands_ = 8, bitpool_ = 53;
  int allocation_ = 0;
  uint8_t buf_[1024];
  uint16_t out_len_ = 0;
  uint8_t frames_ = 0;
  bool opened_ = false;
};

}  // namespace blue_a2dp
