// AAC source codec for A2DP built on an audio-tools style encoder, e.g.
//
//   #include "AudioTools.h"
//   #include "AudioTools/AudioCodecs/CodecAACFDK.h"
//   #include "BTstackA2DP.h"
//   #include "BTstackA2DP/codecs/A2DPCodecAACEncoder.h"
//
//   audio_tools::AACEncoderFDK fdk;
//   blue_a2dp::A2DPCodecAACEncoder<audio_tools::AACEncoderFDK> aac(fdk, 192000);
//   source.add_codec(&aac);      // before source.start()
//
// The encoder must provide setOutput(Print&), audioInfo(), setBitrate(),
// begin(info), end() and write(data, len) and produce ADTS frames (AACEncoderFDK does).
// If the remote device supports AAC it is used, otherwise SBC.
// Needs a lot of RAM and CPU (FDK ~100 KB): a classic ESP32 can do it, a Pico W is tight.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"
#include "AacUtil.h"

namespace blue_a2dp {

template <class Encoder>
class A2DPCodecAACEncoder : public SourceCodec, public Print {
 public:
  /// bit_rate in bit/s: keep it below ~200000 so that one frame fits into a packet
  A2DPCodecAACEncoder(Encoder& encoder, uint32_t bit_rate = 192000)
      : enc_(encoder), bit_rate_(bit_rate) {
    // MPEG-2 AAC LC + MPEG-4 AAC LC, 44.1 / 48 kHz, stereo, VBR, up to 320 kbit/s
    caps_[0] = 0xC0;
    caps_[1] = 0x01;
    caps_[2] = 0x84;
    caps_[3] = 0x84;
    caps_[4] = 0xE2;
    caps_[5] = 0x00;
    memset(cfg_, 0, sizeof(cfg_));
  }

  /// Called with the bitrate that was agreed with the peer (the requested one, lowered to the
  /// peer's maximum), before the encoder starts. With a callback the sketch sets the encoder's
  /// bitrate itself (e.g. fdk.setBitrate(bitrate)); without one it is set automatically.
  void set_bitrate_callback(void (*callback)(uint32_t bitrate)) { bitrate_cb_ = callback; }
  /// Bitrate used for the stream (valid after the configuration)
  uint32_t bitrate() const { return bit_rate_; }

  uint8_t codec_type() const override { return AVDTP_CODEC_MPEG_2_4_AAC; }
  const uint8_t* capabilities(uint8_t& len) override {
    len = sizeof(caps_);
    return caps_;
  }
  uint8_t* config_buffer(uint8_t& len) override {
    len = sizeof(cfg_);
    return cfg_;
  }

  bool select(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CAPABILITY) return false;
    uint8_t object_types = a2dp_subevent_signaling_media_codec_mpeg_aac_capability_get_object_type_bitmap(e);
    uint16_t rates = a2dp_subevent_signaling_media_codec_mpeg_aac_capability_get_sampling_frequency_bitmap(e);
    uint8_t channels = a2dp_subevent_signaling_media_codec_mpeg_aac_capability_get_channels_bitmap(e);
    uint32_t max_rate = a2dp_subevent_signaling_media_codec_mpeg_aac_capability_get_bit_rate(e);

    avdtp_configuration_mpeg_aac_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (object_types & 0x20) {
      cfg.object_type = AVDTP_AAC_MPEG4_LC;
    } else if (object_types & 0x40) {
      cfg.object_type = AVDTP_AAC_MPEG2_LC;
    } else {
      return false;
    }
    if (rates & 0x10) {  // bit n <-> 96000, 88200, 64000, 48000, 44100, ... Hz
      cfg.sampling_frequency = 44100;
    } else if (rates & 0x08) {
      cfg.sampling_frequency = 48000;
    } else {
      return false;
    }
    if (!(channels & 0x04)) return false;  // we send stereo
    cfg.channels = 2;
    cfg.vbr = 0;
    cfg.drc = false;
    cfg.bit_rate = bit_rate_;
    if (max_rate && cfg.bit_rate > max_rate) cfg.bit_rate = max_rate;

    return a2dp_source_set_config_mpeg_aac(
               a2dp_subevent_signaling_media_codec_mpeg_aac_capability_get_a2dp_cid(e), seid,
               a2dp_subevent_signaling_media_codec_mpeg_aac_capability_get_remote_seid(e),
               &cfg) == ERROR_CODE_SUCCESS;
  }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CONFIGURATION) return false;
    if (a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_local_seid(e) != seid)
      return false;
    rate_ = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_sampling_frequency(e);
    uint32_t br = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_bit_rate(e);
    if (br && br < bit_rate_) bit_rate_ = br;
    if (bitrate_cb_) bitrate_cb_(bit_rate_);
    return true;
  }

  uint32_t sample_rate() const override { return rate_; }
  uint16_t frames_per_packet() const override { return 1024; }

  bool open() override {
    if (opened_) return true;
    out_len_ = 0;
    auto info = enc_.audioInfo();
    info.sample_rate = rate_;
    info.channels = 2;
    info.bits_per_sample = 16;
    enc_.setOutput(*this);
    if (!bitrate_cb_) enc_.setBitrate(bit_rate_);
    opened_ = enc_.begin(info);
    return opened_;
  }

  void close() override {
    if (opened_) enc_.end();
    opened_ = false;
  }

  uint16_t encode(const int16_t* pcm, uint8_t* out, uint16_t max_len) override {
    if (!opened_) return 0;
    out_len_ = 0;
    enc_.write((const uint8_t*)pcm, 1024 * 2 * sizeof(int16_t));
    const uint8_t* frame = nullptr;
    uint16_t used = 0;
    uint16_t n = aac::adts_frame(adts_, out_len_, &frame, &used);
    if (n == 0) return 0;  // encoder is still filling its pipeline
    return aac::latm_wrap(frame, n, out, max_len);
  }

  // Print: ADTS output of the encoder
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (out_len_ + len > sizeof(adts_)) len = sizeof(adts_) - out_len_;
    memcpy(adts_ + out_len_, data, len);
    out_len_ += len;
    return len;
  }

 private:
  Encoder& enc_;
  uint32_t bit_rate_;
  uint32_t rate_ = 44100;
  uint8_t caps_[6];
  uint8_t cfg_[6];
  uint8_t adts_[2048];
  uint16_t out_len_ = 0;
  bool opened_ = false;
  void (*bitrate_cb_)(uint32_t) = nullptr;
};

}  // namespace blue_a2dp
