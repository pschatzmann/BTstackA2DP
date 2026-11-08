// EXPERIMENTAL: MP3 (MPEG-1,2 Audio) source codec for A2DP built on an audio-tools encoder:
//
//   audio_tools::MP3EncoderShine mp3_encoder;
//   blue_a2dp::A2DPCodecMP3Encoder<audio_tools::MP3EncoderShine> mp3(mp3_encoder);
//   source.add_codec(&mp3);      // before source.start()
//
// The encoder must provide setOutput(Print&), audioInfo()/setAudioInfo(), setBitrate(kbps),
// begin() and end() and write one MP3 frame per 1152 input frames (Shine does). Used when the
// speaker offers MP3 (rare), otherwise SBC.
#pragma once

#include <Print.h>

#include "../A2DPCodec.h"
#include "../BluetoothA2DPCommon.h"
#include "Mp3Util.h"

namespace blue_a2dp {

template <class Encoder>
class A2DPCodecMP3Encoder : public SourceCodec, public Print {
 public:
  explicit A2DPCodecMP3Encoder(Encoder& encoder) : enc_(encoder) {
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

  bool select(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AUDIO_CAPABILITY) return false;
    if (!(a2dp_subevent_signaling_media_codec_mpeg_audio_capability_get_layer_bitmap(e) & 0x01)) return false;  // layer III
    if (!(a2dp_subevent_signaling_media_codec_mpeg_audio_capability_get_channel_mode_bitmap(e) & 0x04)) return false;  // stereo
    uint32_t rate = mp3::choose_sampling_frequency(
        a2dp_subevent_signaling_media_codec_mpeg_audio_capability_get_sampling_frequency_bitmap(e));
    int index = mp3::choose_bit_rate_index(
        a2dp_subevent_signaling_media_codec_mpeg_audio_capability_get_bit_rate_index_bitmap(e));
    if (rate == 0 || index < 0) return false;

    avdtp_configuration_mpeg_audio_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.layer = AVDTP_MPEG_LAYER_3;
    cfg.crc = 0;
    cfg.channel_mode = AVDTP_CHANNEL_MODE_STEREO;
    cfg.media_payload_format = 0;  // RFC 2250 payload header
    cfg.sampling_frequency = rate;
    cfg.vbr = 0;
    cfg.bit_rate_index = index;
    return a2dp_source_set_config_mpeg_audio(
               a2dp_subevent_signaling_media_codec_mpeg_audio_capability_get_a2dp_cid(e), seid,
               a2dp_subevent_signaling_media_codec_mpeg_audio_capability_get_remote_seid(e),
               &cfg) == ERROR_CODE_SUCCESS;
  }

  bool configure(const uint8_t* e) override {
    if (e[2] != A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AUDIO_CONFIGURATION) return false;
    if (a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_local_seid(e) != seid)
      return false;
    rate_ = a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_sampling_frequency(e);
    int kbps = mp3::bit_rate_kbps(a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_bit_rate_index(e));
    if (kbps) kbps_ = kbps;
    return rate_ != 0;
  }

  uint32_t sample_rate() const override { return rate_; }
  /// one MP3 frame (1152 samples per channel) per packet
  uint16_t frames_per_packet() const override { return 1152; }

  bool open() override {
    if (opened_) return true;
    out_len_ = 0;
    auto info = enc_.audioInfo();
    info.sample_rate = rate_;
    info.channels = 2;
    info.bits_per_sample = 16;
    enc_.setAudioInfo(info);
    enc_.setBitrate(kbps_);
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
    if (out_len_ == 0 || (uint32_t)out_len_ + mp3::kPayloadHeaderLen > max_len) return 0;
    memset(out, 0, mp3::kPayloadHeaderLen);  // RFC 2250: MBZ + fragmentation offset 0
    memcpy(out + mp3::kPayloadHeaderLen, buf_, out_len_);
    return out_len_ + mp3::kPayloadHeaderLen;
  }

  // Print: MP3 output of the encoder
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (out_len_ + len > sizeof(buf_)) return len;
    memcpy(buf_ + out_len_, data, len);
    out_len_ += len;
    return len;
  }

 private:
  Encoder& enc_;
  uint32_t rate_ = 44100;
  int kbps_ = 128;
  uint8_t caps_[4];
  uint8_t cfg_[4];
  uint8_t buf_[1024];
  uint16_t out_len_ = 0;
  bool opened_ = false;
};

}  // namespace blue_a2dp
