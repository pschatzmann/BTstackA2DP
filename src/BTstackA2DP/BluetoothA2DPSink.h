// A2DP sink (Bluetooth speaker / receiver): receives SBC from a phone or PC, decodes it
// and hands 16 bit PCM to the application. API modelled after ESP32-A2DP.
#pragma once

#include <stddef.h>

#include <utility>

#include "A2DPCodec.h"
#include "BluetoothA2DPCommon.h"
#include <btstack/classic/btstack_sbc_bluedroid.h>

namespace blue_a2dp {

namespace detail {
// Duck typing for BluetoothA2DPSink::set_output(): no dependency on Arduino or audio-tools
template <class T>
using enable_if_output_t = decltype(std::declval<T&>().write((const uint8_t*)nullptr, (size_t)0));

template <class T>
auto call_begin(T* o, int) -> decltype(o->begin(), bool()) {
  o->begin();
  return true;
}
template <class T>
bool call_begin(T*, long) {
  return true;  // no begin()
}

// audio-tools: audioInfo() / setAudioInfo(AudioInfo) with sample_rate, channels, bits_per_sample
template <class T>
auto call_set_audio_info(T* o, const AudioInfo& info, int)
    -> decltype(o->setAudioInfo(o->audioInfo()), void()) {
  auto ai = o->audioInfo();
  if ((uint32_t)ai.sample_rate == info.sample_rate && ai.channels == info.channels &&
      ai.bits_per_sample == info.bits_per_sample)
    return;
  ai.sample_rate = info.sample_rate;
  ai.channels = info.channels;
  ai.bits_per_sample = info.bits_per_sample;
  o->setAudioInfo(ai);
}
template <class T>
void call_set_audio_info(T*, const AudioInfo&, long) {}
}  // namespace detail

class BluetoothA2DPSink : public BluetoothA2DPCommon, public PcmOutput {
 public:
  BluetoothA2DPSink() {
    // we support all SBC configurations with bitpool 2-53
    caps_[0] = 0xFF;
    caps_[1] = 0xFF;
    caps_[2] = 2;
    caps_[3] = 53;
    memset(cfg_, 0, sizeof(cfg_));
  }

  /// Writes the PCM data to 'out' (as in ESP32-A2DP: BluetoothA2DPSink a2dp_sink(i2s);).
  /// See set_output().
  template <class T, class = detail::enable_if_output_t<T>>
  explicit BluetoothA2DPSink(T& out) : BluetoothA2DPSink() {
    set_output(out);
  }

  /// Writes the decoded PCM data (16 bit stereo) to 'out': anything with
  /// write(const uint8_t*, size_t), e.g. an Arduino Print or an audio-tools stream such as
  /// I2SStream. If 'out' has begin() it is called by start(); if it has audioInfo() and
  /// setAudioInfo() (audio-tools) it gets the sample rate of the stream. Like in ESP32-A2DP the
  /// data is written from the Bluetooth task: a blocking write (I2S) paces the stream. For
  /// heavy processing use set_stream_reader() with a queue instead (see examples/sink-i2s).
  template <class T, class = detail::enable_if_output_t<T>>
  void set_output(T& out) {
    out_obj_ = &out;
    out_write_ = &out_write_thunk<T>;
    out_begin_ = &out_begin_thunk<T>;
    out_info_ = &out_info_thunk<T>;
  }

  /// Starts the Bluetooth stack and makes the device discoverable as 'name'.
  bool start(const char* name = "BTstackA2DP", bool auto_reconnect = true) {
    set_name(name);
    set_auto_reconnect(auto_reconnect);
    if (out_obj_ && !out_begun_) {
      out_begin_(out_obj_);
      out_begun_ = true;
    }
    return start_stack();
  }

  /// Adds an optional codec (e.g. AAC, see codecs/A2DPCodecAACDecoder.h) next to the built-in SBC.
  /// Call before start(). Returns false if no more codecs fit.
  bool add_codec(SinkCodec* codec) {
    if (started_ || codec_count_ >= kMaxCodecs) return false;
    codecs_[codec_count_++] = codec;
    return true;
  }

  /// Replaces the SBC decoder of btstack, e.g. by the one of audio-tools (see
  /// codecs/A2DPCodecSBCDecoder.h). Call before start().
  void set_sbc_codec(SinkCodec* codec) { sbc_codec_ = codec; }

  /// Registers the receiver of the decoded PCM data (interleaved 16 bit samples). The
  /// callback runs in the Bluetooth task and should not block for long.
  /// 'also_output' exists for source compatibility with ESP32-A2DP and is ignored.
  void set_stream_reader(StreamReaderCallback cb, bool also_output = false) {
    (void)also_output;
    reader_ = cb;
  }
  /// Called after each decoded packet was passed to the stream reader
  void set_on_data_received(DataReceivedCallback cb) { data_received_cb_ = cb; }
  /// Called when the sample rate / channel count of the stream is known or changes
  void set_audio_info_callback(AudioInfoCallback cb, void* obj = nullptr) {
    audio_info_cb_ = cb;
    audio_info_obj_ = obj;
  }
  /// Apply the volume (set locally or by the remote device) to the PCM data (default: on)
  void set_volume_control(bool active) { software_volume_ = active; }
  /// Format of the PCM data. Valid after the audio state is 'Started'
  AudioInfo get_audio_info() const { return info_; }
  uint32_t get_sample_rate() const { return info_.sample_rate; }

 protected:
  bool is_sink() const override { return true; }
  uint32_t class_of_device() const override { return 0x240414; }  // audio, loudspeaker

  bool setup_role() override {
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&a2dp_cb);
    a2dp_sink_register_media_handler(&media_cb);
    avdtp_stream_endpoint_t* ep = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC, caps_, sizeof(caps_), cfg_, sizeof(cfg_));
    if (!ep) return false;
    local_seid_ = avdtp_local_seid(ep);
    if (sbc_codec_) sbc_codec_->seid = local_seid_;

    // optional codecs: each gets its own stream endpoint. If btstack has no room for it
    // (MAX_NR_AVDTP_STREAM_ENDPOINTS), the codec is skipped and SBC is used.
    for (int i = 0; i < codec_count_; i++) {
      uint8_t caps_len = 0, cfg_len = 0;
      const uint8_t* caps = codecs_[i]->capabilities(caps_len);
      uint8_t* cfg = codecs_[i]->config_buffer(cfg_len);
      avdtp_stream_endpoint_t* cep = a2dp_sink_create_stream_endpoint(
          AVDTP_AUDIO, (avdtp_media_codec_type_t)codecs_[i]->codec_type(), (uint8_t*)caps,
          caps_len, cfg, cfg_len);
      codecs_[i]->seid = cep ? avdtp_local_seid(cep) : 0xFF;
    }

    memset(sdp_sink_, 0, sizeof(sdp_sink_));
    a2dp_sink_create_sdp_record(sdp_sink_, sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE, nullptr, nullptr);
    sdp_register_service(sdp_sink_);
    return true;
  }

  void on_avrcp_connected() override {
    avrcp_target_support_event(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
    // btstack answers the remote's volume registration with its own copy of the volume,
    // which starts at 0: without this a source with software volume would mute itself
    avrcp_target_adjust_absolute_volume(avrcp_cid_, (uint8_t)volume_);
    avrcp_target_support_event(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_BATT_STATUS_CHANGED);
    avrcp_target_battery_status_changed(avrcp_cid_, AVRCP_BATTERY_STATUS_NORMAL);
    avrcp_controller_get_supported_events(avrcp_cid_);
  }

  void do_connect_to_peer() override {
    if (!have_peer_) return;
    if (a2dp_sink_establish_stream(peer_, &a2dp_cid_) == ERROR_CODE_SUCCESS)
      set_connection_state(ConnectionState::Connecting);
  }
  void do_disconnect_peer() override {
    if (a2dp_cid_) {
      set_connection_state(ConnectionState::Disconnecting);
      a2dp_sink_disconnect(a2dp_cid_);
    }
  }
  void do_send_volume() override {
    if (avrcp_cid_) avrcp_target_volume_changed(avrcp_cid_, (uint8_t)volume_);
  }

 private:
  // ---- media path ------------------------------------------------------------------------
  static void media_cb(uint8_t seid, uint8_t* packet, uint16_t size) {
    (void)seid;
    BluetoothA2DPSink* self = static_cast<BluetoothA2DPSink*>(active());
    if (self) self->handle_media(seid, packet, size);
  }

  SinkCodec* codec_for(uint8_t seid) {
    for (int i = 0; i < codec_count_; i++)
      if (codecs_[i]->seid == seid) return codecs_[i];
    return nullptr;
  }

  void handle_media(uint8_t seid, uint8_t* packet, uint16_t size) {
    // codecs without RTP header get the complete packet
    if (active_codec_ && seid == active_codec_->seid && !active_codec_->uses_rtp()) {
      active_codec_->decode(packet, size);
      return;
    }
    if (size < 13) return;
    // RTP header (12 bytes + CSRC list)
    int pos = 12 + 4 * (packet[0] & 0x0F);
    if (active_codec_ && seid == active_codec_->seid) {
      if (pos < size) active_codec_->decode(packet + pos, size - pos);
      return;
    }
    if (!decoder_) return;
    // SBC: followed by the 1 byte SBC payload header
    if (pos + 1 >= size) return;
    pos++;  // skip SBC header (fragmentation / number of frames)
    decoder_->decode_signed_16(&decoder_ctx_, 0, packet + pos, size - pos);
  }

  static void pcm_cb(int16_t* data, int frames, int channels, int rate, void* ctx) {
    static_cast<BluetoothA2DPSink*>(ctx)->handle_pcm(data, frames, channels, rate);
  }

  // PcmOutput: decoded data of the optional codecs
  void on_pcm(int16_t* data, int frames, int channels, int rate) override {
    handle_pcm(data, frames, channels, rate);
  }

  void handle_pcm(int16_t* data, int frames, int channels, int rate) {
    if ((uint32_t)rate != info_.sample_rate || channels != info_.channels) {
      info_.sample_rate = rate;
      info_.channels = channels;
      if (out_info_) out_info_(out_obj_, info_);
      if (audio_info_cb_) audio_info_cb_(info_, audio_info_obj_);
    }
    int samples = frames * channels;
    if (software_volume_ && volume_ < 127) {
      int32_t gain = (volume_ * 32768) / 127;
      for (int i = 0; i < samples; i++) data[i] = (int16_t)((data[i] * gain) >> 15);
    }
    if (out_write_) out_write_(out_obj_, (const uint8_t*)data, samples * sizeof(int16_t));
    if (reader_) reader_((const uint8_t*)data, samples * sizeof(int16_t));
    if (data_received_cb_) data_received_cb_();
  }

  // ---- A2DP events -----------------------------------------------------------------------
  static void a2dp_cb(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    BluetoothA2DPSink* self = static_cast<BluetoothA2DPSink*>(active());
    if (type != HCI_EVENT_PACKET || !self) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;
    self->handle_a2dp(packet);
  }

  void handle_a2dp(uint8_t* packet) {
    BTSTACKA2DP_LOG("a2dp event 0x%02x", packet[2]);
    switch (packet[2]) {
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        reconfigure_ = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(packet);
        pending_codec_ = (sbc_codec_ && sbc_codec_->configure(packet)) ? sbc_codec_ : nullptr;
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AUDIO_CONFIGURATION:  // MP3
        reconfigure_ = a2dp_subevent_signaling_media_codec_mpeg_audio_configuration_get_reconfigure(packet);
        pending_codec_ = nullptr;
        for (int i = 0; i < codec_count_; i++)
          if (codecs_[i]->configure(packet)) pending_codec_ = codecs_[i];
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:  // vendor codecs, e.g. aptX
        reconfigure_ = a2dp_subevent_signaling_media_codec_other_configuration_get_reconfigure(packet);
        pending_codec_ = nullptr;
        for (int i = 0; i < codec_count_; i++)
          if (codecs_[i]->configure(packet)) pending_codec_ = codecs_[i];
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CONFIGURATION:
        reconfigure_ = a2dp_subevent_signaling_media_codec_mpeg_aac_configuration_get_reconfigure(packet);
        pending_codec_ = nullptr;
        for (int i = 0; i < codec_count_; i++)
          if (codecs_[i]->configure(packet)) pending_codec_ = codecs_[i];
        break;
      case A2DP_SUBEVENT_STREAM_ESTABLISHED:
        if (a2dp_subevent_stream_established_get_status(packet) != ERROR_CODE_SUCCESS) {
          a2dp_cid_ = 0;
          set_connection_state(ConnectionState::Disconnected);
          schedule_reconnect();
          break;
        }
        a2dp_subevent_stream_established_get_bd_addr(packet, peer_);
        have_peer_ = true;
        a2dp_cid_ = a2dp_subevent_stream_established_get_a2dp_cid(packet);
        set_connection_state(ConnectionState::Connected);
        // we send the AVRCP controls (play / pause / next) ourselves, so open the AVRCP channel
        // to the same device unless the remote side already did
        if (!avrcp_cid_) {
          uint16_t cid = 0;
          uint8_t status = avrcp_connect(peer_, &cid);
          BTSTACKA2DP_LOG("avrcp connect to %s: status 0x%02x", bd_addr_to_str(peer_), status);
        }
        break;
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
        if (a2dp_subevent_signaling_connection_established_get_status(packet) != ERROR_CODE_SUCCESS) {
          bd_addr_t addr;
          a2dp_subevent_signaling_connection_established_get_bd_addr(packet, addr);
          a2dp_cid_ = 0;
          set_connection_state(ConnectionState::Disconnected);
          handle_connection_failure(a2dp_subevent_signaling_connection_established_get_status(packet), addr);
        }
        break;
      case A2DP_SUBEVENT_STREAM_STARTED:
        if (reconfigure_) close_decoder();
        active_codec_ = pending_codec_;
        if (active_codec_) {
          active_codec_->open(this);
        } else {
          open_decoder();
        }
        set_audio_state(AudioState::Started);
        break;
      case A2DP_SUBEVENT_STREAM_SUSPENDED:
        set_audio_state(AudioState::Suspended);
        break;
      case A2DP_SUBEVENT_STREAM_RELEASED:
        close_decoder();
        set_audio_state(AudioState::Stopped);
        break;
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        a2dp_cid_ = 0;
        close_decoder();
        set_audio_state(AudioState::Stopped);
        set_connection_state(ConnectionState::Disconnected);
        schedule_reconnect();
        break;
      default:
        break;
    }
  }

  void open_decoder() {
    if (decoder_) return;
    decoder_ = btstack_sbc_decoder_bluedroid_init_instance(&decoder_ctx_);
    decoder_->configure(&decoder_ctx_, SBC_MODE_STANDARD, &pcm_cb, this);
  }
  void close_decoder() {
    decoder_ = nullptr;
    if (active_codec_) active_codec_->close();
    active_codec_ = nullptr;
  }

  uint8_t caps_[4];
  uint8_t cfg_[4];
  uint8_t sdp_sink_[150];
  uint8_t local_seid_ = 0;
  uint8_t reconfigure_ = 0;

  static const int kMaxCodecs = 2;
  SinkCodec* codecs_[kMaxCodecs] = {nullptr, nullptr};
  int codec_count_ = 0;
  SinkCodec* sbc_codec_ = nullptr;       // replaces the built-in SBC decoder
  SinkCodec* pending_codec_ = nullptr;  // selected by the last configuration event
  SinkCodec* active_codec_ = nullptr;   // decoding the running stream

  const btstack_sbc_decoder_t* decoder_ = nullptr;
  btstack_sbc_decoder_bluedroid_t decoder_ctx_;

  AudioInfo info_;
  bool software_volume_ = true;
  StreamReaderCallback reader_ = nullptr;
  // output object of set_output() (type erased)
  void* out_obj_ = nullptr;
  void (*out_write_)(void*, const uint8_t*, size_t) = nullptr;
  bool (*out_begin_)(void*) = nullptr;
  void (*out_info_)(void*, const AudioInfo&) = nullptr;
  bool out_begun_ = false;

  template <class T>
  static void out_write_thunk(void* o, const uint8_t* data, size_t len) {
    T* out = static_cast<T*>(o);
    // a stream may accept less than offered (e.g. a full I2S buffer): keep writing
    size_t done = 0;
    for (int tries = 0; done < len && tries < 100; tries++) done += out->write(data + done, len - done);
  }
  template <class T>
  static bool out_begin_thunk(void* o) {
    return detail::call_begin(static_cast<T*>(o), 0);
  }
  template <class T>
  static void out_info_thunk(void* o, const AudioInfo& info) {
    detail::call_set_audio_info(static_cast<T*>(o), info, 0);
  }
  DataReceivedCallback data_received_cb_ = nullptr;
  AudioInfoCallback audio_info_cb_ = nullptr;
  void* audio_info_obj_ = nullptr;
};

}  // namespace blue_a2dp

typedef blue_a2dp::BluetoothA2DPSink BluetoothA2DPSink;
