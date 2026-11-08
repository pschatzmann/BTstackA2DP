// A2DP source (Bluetooth transmitter): pulls PCM from the application, encodes it to SBC
// and streams it to a speaker or headset. API modelled after ESP32-A2DP.
#pragma once

#include "A2DPCodec.h"
#include "BluetoothA2DPCommon.h"
#include <btstack/classic/btstack_sbc_bluedroid.h>

namespace blue_a2dp {

class BluetoothA2DPSource : public BluetoothA2DPCommon {
 public:
  BluetoothA2DPSource() {
    // 44.1 / 48 kHz, stereo / joint stereo, all block lengths, subbands, allocation methods
    caps_[0] = (AVDTP_SBC_44100 << 4) | (AVDTP_SBC_48000 << 4) | AVDTP_SBC_STEREO;
    caps_[1] = 0xFF;
    caps_[2] = 2;
    caps_[3] = 53;
    memset(cfg_, 0, sizeof(cfg_));
    memset(&media_, 0, sizeof(media_));
    memset(&sbc_cfg_, 0, sizeof(sbc_cfg_));
    memset(pending_, 0, sizeof(pending_));
  }

  /// Starts the stack and connects to the device named 'remote_name' (searched by
  /// inquiry). With an empty name / nullptr the first audio device is used, unless a
  /// device callback is set. 'cb' provides the audio data.
  bool start(const char* remote_name, FrameDataCallback cb = nullptr) {
    if (cb) frame_cb_ = cb;
    return start_scan(remote_name);
  }
  /// Same, but the data callback delivers raw bytes of 16 bit stereo PCM
  bool start_raw(const char* remote_name, RawDataCallback cb) {
    raw_cb_ = cb;
    return start_scan(remote_name);
  }
  /// Starts the stack and connects directly to a known device address
  bool start_with_address(const uint8_t* addr, FrameDataCallback cb = nullptr) {
    if (cb) frame_cb_ = cb;
    memcpy(peer_, addr, 6);
    have_peer_ = true;
    scan_ = false;
    connect_on_start_ = true;
    return start_stack();
  }

  /// Adds an optional codec (e.g. AAC, see codecs/A2DPCodecAACEncoder.h). It is used when
  /// the remote device supports it, otherwise SBC. Call before start().
  bool add_codec(SourceCodec* codec) {
    if (started_ || codec_count_ >= kMaxCodecs) return false;
    codecs_[codec_count_++] = codec;
    return true;
  }

  /// Replaces the SBC encoder of btstack, e.g. by the one of audio-tools (see
  /// codecs/A2DPCodecSBCEncoder.h). Call before start().
  void set_sbc_codec(SourceCodec* codec) { sbc_codec_ = codec; }

  /// Track information sent to the remote device over AVRCP (shown on the speaker / phone).
  /// Can be called at any time; it is sent again on every new AVRCP connection.
  void set_now_playing(const char* title, const char* artist = nullptr, const char* album = nullptr) {
    strncpy(np_title_, title ? title : "", sizeof(np_title_) - 1);
    strncpy(np_artist_, artist ? artist : "", sizeof(np_artist_) - 1);
    strncpy(np_album_, album ? album : "", sizeof(np_album_) - 1);
    np_title_[sizeof(np_title_) - 1] = np_artist_[sizeof(np_artist_) - 1] = np_album_[sizeof(np_album_) - 1] = 0;
    // before start() the text is sent on the first AVRCP connection; afterwards it is pushed now
    if (started_) Port::post(&push_now_playing, this);
  }

  void set_data_callback_in_frames(FrameDataCallback cb) { frame_cb_ = cb; }
  void set_data_callback(RawDataCallback cb) { raw_cb_ = cb; }
  /// Called for each discovered audio device; return true to connect to it
  void set_ssid_callback(DeviceFoundCallback cb) { device_cb_ = cb; }
  /// Name of this device as seen by other devices
  void set_local_name(const char* name) { set_name(name); }
  /// Apply the volume to the PCM data (default: on)
  void set_volume_control(bool active) { software_volume_ = active; }
  /// Sample rate negotiated with the sink (0 until a stream has been configured)
  uint32_t get_sample_rate() const {
    return active_codec_ ? active_codec_->sample_rate() : sbc_cfg_.sampling_frequency;
  }

 protected:
  bool is_sink() const override { return false; }
  uint32_t class_of_device() const override { return 0x200408; }  // audio, headset/handsfree

  bool setup_role() override {
    a2dp_source_init();
    a2dp_source_register_packet_handler(&a2dp_cb);
    avdtp_stream_endpoint_t* ep = a2dp_source_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC, caps_, sizeof(caps_), cfg_, sizeof(cfg_));
    if (!ep) return false;
    avdtp_set_preferred_sampling_frequency(ep, 44100);
    media_.local_seid = avdtp_local_seid(ep);
    if (sbc_codec_) sbc_codec_->seid = media_.local_seid;
    avdtp_source_register_delay_reporting_category(media_.local_seid);

    // optional codecs, one stream endpoint each (skipped if btstack has no room)
    for (int i = 0; i < codec_count_; i++) {
      uint8_t caps_len = 0, cfg_len = 0;
      const uint8_t* caps = codecs_[i]->capabilities(caps_len);
      uint8_t* cfg = codecs_[i]->config_buffer(cfg_len);
      avdtp_stream_endpoint_t* cep = a2dp_source_create_stream_endpoint(
          AVDTP_AUDIO, (avdtp_media_codec_type_t)codecs_[i]->codec_type(), (uint8_t*)caps,
          caps_len, cfg, cfg_len);
      codecs_[i]->seid = cep ? avdtp_local_seid(cep) : 0xFF;
    }

    memset(sdp_source_, 0, sizeof(sdp_source_));
    a2dp_source_create_sdp_record(sdp_source_, sdp_create_service_record_handle(),
                                  AVDTP_SOURCE_FEATURE_MASK_PLAYER, nullptr, nullptr);
    sdp_register_service(sdp_source_);
    return true;
  }

  void on_hci_working() override {
    if (connect_on_start_ && have_peer_) {
      do_connect_to_peer();
    } else if (scan_) {
      start_inquiry();
    }
  }

  void on_avrcp_connected() override {
    apply_now_playing();
    avrcp_target_support_event(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
    avrcp_target_support_event(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
    avrcp_target_support_event(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
    avrcp_target_set_now_playing_info(avrcp_cid_, nullptr, 1);
    avrcp_controller_enable_notification(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
  }

  void on_avrcp_target_event(uint8_t* packet) override {
    // some sinks ask for the play status and wait for the answer
    if (packet[2] == AVRCP_SUBEVENT_PLAY_STATUS_QUERY) {
      avrcp_target_play_status(avrcp_cid_, 0xFFFFFFFFu, 0xFFFFFFFFu,
                               media_.streaming ? AVRCP_PLAYBACK_STATUS_PLAYING
                                                : AVRCP_PLAYBACK_STATUS_PAUSED);
    }
  }

  void on_avrcp_operation(avrcp_operation_id_t op, bool pressed) override {
    if (!pressed) return;
    switch (op) {
      case AVRCP_OPERATION_ID_PLAY:
        do_play();
        break;
      case AVRCP_OPERATION_ID_PAUSE:
        do_pause();
        break;
      case AVRCP_OPERATION_ID_STOP:
        do_stop();
        break;
      default:
        BluetoothA2DPCommon::on_avrcp_operation(op, pressed);
        break;
    }
  }

  // for a source, play/pause/stop control our own stream
  void do_play() override {
    if (a2dp_cid_) a2dp_source_start_stream(a2dp_cid_, media_.local_seid);
  }
  void do_pause() override {
    if (a2dp_cid_) a2dp_source_pause_stream(a2dp_cid_, media_.local_seid);
  }
  void do_stop() override {
    if (a2dp_cid_) a2dp_source_stop_stream(a2dp_cid_, media_.local_seid);
  }

  void do_connect_to_peer() override {
    if (!have_peer_ || connection_state_ != ConnectionState::Disconnected) return;
    uint8_t status = a2dp_source_establish_stream(peer_, &a2dp_cid_);
    BLUEA2DP_LOG("connect to %s: status 0x%02x", bd_addr_to_str(peer_), status);
    if (status == ERROR_CODE_SUCCESS) set_connection_state(ConnectionState::Connecting);
  }
  void do_disconnect_peer() override {
    if (a2dp_cid_) {
      set_connection_state(ConnectionState::Disconnecting);
      a2dp_source_disconnect(a2dp_cid_);
    }
  }
  void do_send_volume() override {
    if (avrcp_cid_) avrcp_controller_set_absolute_volume(avrcp_cid_, (uint8_t)volume_);
  }

  static void push_now_playing(void* p) { static_cast<BluetoothA2DPSource*>(p)->apply_now_playing(); }

  void apply_now_playing() {
    if (!avrcp_cid_ || !np_title_[0]) return;
    avrcp_track_t track;
    memset(&track, 0, sizeof(track));
    track.title = np_title_;
    track.artist = np_artist_;
    track.album = np_album_;
    avrcp_target_set_now_playing_info(avrcp_cid_, &track, 1);
  }

  // ---- discovery -------------------------------------------------------------------------
  void on_hci_event(uint8_t* packet) override {
    switch (hci_event_packet_get_type(packet)) {
      case GAP_EVENT_INQUIRY_RESULT:
        if (scan_) handle_inquiry_result(packet);
        break;
      case GAP_EVENT_INQUIRY_COMPLETE:
        if (scan_) {
          if (pending_count_) {
            request_next_name();
          } else {
            start_inquiry();
          }
        }
        break;
      case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
        if (scan_ && requesting_name_) handle_remote_name(packet);
        break;
      default:
        break;
    }
  }

 private:
  static const int kMaxPending = 8;
  static const uint32_t kAudioTimeoutMs = 10;
  static const int kStorageSize = 1030;

  bool start_scan(const char* remote_name) {
    want_name_[0] = 0;
    if (remote_name) strncpy(want_name_, remote_name, sizeof(want_name_) - 1);
    scan_ = true;
    return start_stack();
  }

  /// Ends the search for a device. Inquiry and name requests take radio time away from an
  /// active stream, so this must not go on once a connection exists.
  void stop_scanning() {
    if (!scan_) return;
    scan_ = false;
    requesting_name_ = false;
    pending_count_ = 0;
    gap_inquiry_stop();
  }

  void start_inquiry() {
    pending_count_ = 0;
    requesting_name_ = false;
    gap_inquiry_start(12);  // 12 * 1.28s
  }

  void handle_inquiry_result(uint8_t* packet) {
    bd_addr_t addr;
    gap_event_inquiry_result_get_bd_addr(packet, addr);
    uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
    const uint32_t audio_cod = 0x200000 | 0x000400;  // service class audio + major class audio
    if ((cod & audio_cod) != audio_cod) return;
    int rssi = gap_event_inquiry_result_get_rssi_available(packet)
                   ? (int8_t)gap_event_inquiry_result_get_rssi(packet)
                   : 0;
    if (gap_event_inquiry_result_get_name_available(packet)) {
      char name[sizeof(want_name_)];
      int len = gap_event_inquiry_result_get_name_len(packet);
      if (len > (int)sizeof(name) - 1) len = sizeof(name) - 1;
      memcpy(name, gap_event_inquiry_result_get_name(packet), len);
      name[len] = 0;
      check_candidate(name, addr, rssi);
    } else if (pending_count_ < kMaxPending) {
      Pending& p = pending_[pending_count_++];
      memcpy(p.addr, addr, 6);
      p.psrm = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
      p.clock_offset = gap_event_inquiry_result_get_clock_offset(packet);
      p.rssi = rssi;
    }
  }

  void request_next_name() {
    if (pending_count_ == 0) {
      requesting_name_ = false;
      start_inquiry();
      return;
    }
    current_ = pending_[--pending_count_];
    requesting_name_ = true;
    gap_remote_name_request(current_.addr, current_.psrm, current_.clock_offset | 0x8000);
  }

  void handle_remote_name(uint8_t* packet) {
    bd_addr_t addr;
    hci_event_remote_name_request_complete_get_bd_addr(packet, addr);
    if (memcmp(addr, current_.addr, 6) != 0) return;
    if (hci_event_remote_name_request_complete_get_status(packet) == ERROR_CODE_SUCCESS) {
      char name[sizeof(want_name_)];
      strncpy(name, hci_event_remote_name_request_complete_get_remote_name(packet), sizeof(name) - 1);
      name[sizeof(name) - 1] = 0;
      if (check_candidate(name, addr, current_.rssi)) return;
    }
    request_next_name();
  }

  /// Connects if the device is what we are looking for
  bool check_candidate(const char* name, const bd_addr_t addr, int rssi) {
    bool take;
    if (device_cb_) {
      take = device_cb_(name, addr, rssi);
    } else if (want_name_[0]) {
      take = strcmp(name, want_name_) == 0;
    } else {
      take = true;
    }
    if (!take) return false;
    scan_ = false;
    requesting_name_ = false;
    gap_inquiry_stop();
    memcpy(peer_, addr, 6);
    have_peer_ = true;
    do_connect_to_peer();
    return true;
  }

  // ---- A2DP events -----------------------------------------------------------------------
  static void a2dp_cb(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    BluetoothA2DPSource* self = static_cast<BluetoothA2DPSource*>(active());
    if (type != HCI_EVENT_PACKET || !self) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;
    self->handle_a2dp(packet);
  }

  void handle_a2dp(uint8_t* packet) {
    // not for the "can send" event: it comes for every media packet and logging it (e.g. over a
    // slow serial line) would stall the Bluetooth task
    if (packet[2] != A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW)
      BLUEA2DP_LOG("a2dp event 0x%02x", packet[2]);
    switch (packet[2]) {
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
        if (a2dp_subevent_signaling_connection_established_get_status(packet) != ERROR_CODE_SUCCESS) {
          a2dp_cid_ = 0;
          set_connection_state(ConnectionState::Disconnected);
          schedule_reconnect();
          break;
        }
        a2dp_cid_ = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
        a2dp_subevent_signaling_connection_established_get_bd_addr(packet, peer_);
        have_peer_ = true;
        stop_scanning();  // connected (maybe by the remote device): searching would disturb the stream
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        close_codec();
        if (sbc_codec_ && sbc_codec_->configure(packet)) {
          active_codec_ = sbc_codec_;
        } else {
          configure_sbc(packet);
        }
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CAPABILITY:
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AUDIO_CAPABILITY:  // MP3
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CAPABILITY:  // vendor codecs, e.g. aptX
        // btstack only selects SBC by itself: choosing a configuration here (while the
        // capabilities are still being read) overrides that choice
        for (int i = 0; i < codec_count_; i++)
          if (codecs_[i]->seid != 0xFF && codecs_[i]->select(packet)) break;
        break;
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AAC_CONFIGURATION:
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_MPEG_AUDIO_CONFIGURATION:
      case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:
        close_codec();
        for (int i = 0; i < codec_count_; i++)
          if (codecs_[i]->configure(packet)) active_codec_ = codecs_[i];
        break;
      case A2DP_SUBEVENT_STREAM_ESTABLISHED:
        if (a2dp_subevent_stream_established_get_status(packet) != ERROR_CODE_SUCCESS) {
          set_connection_state(ConnectionState::Disconnected);
          break;
        }
        set_connection_state(ConnectionState::Connected);
        a2dp_source_start_stream(a2dp_cid_, media_.local_seid);
        break;
      case A2DP_SUBEVENT_STREAM_RECONFIGURED:
        if (a2dp_subevent_stream_reconfigured_get_status(packet) == ERROR_CODE_SUCCESS)
          a2dp_source_start_stream(a2dp_cid_, media_.local_seid);
        break;
      case A2DP_SUBEVENT_STREAM_STARTED:
        if (avrcp_cid_) avrcp_target_set_playback_status(avrcp_cid_, AVRCP_PLAYBACK_STATUS_PLAYING);
        timer_start();
        set_audio_state(AudioState::Started);
        break;
      case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
        send_media_packet();
        break;
      case A2DP_SUBEVENT_STREAM_SUSPENDED:
        if (avrcp_cid_) avrcp_target_set_playback_status(avrcp_cid_, AVRCP_PLAYBACK_STATUS_PAUSED);
        timer_stop();
        set_audio_state(AudioState::Suspended);
        break;
      case A2DP_SUBEVENT_STREAM_RELEASED:
        timer_stop();
        close_codec();
        set_audio_state(AudioState::Stopped);
        break;
      case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
        timer_stop();
        close_codec();
        a2dp_cid_ = 0;
        set_audio_state(AudioState::Stopped);
        set_connection_state(ConnectionState::Disconnected);
        schedule_reconnect();
        break;
      default:
        break;
    }
  }

  void configure_sbc(uint8_t* packet) {
    sbc_cfg_.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
    sbc_cfg_.num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
    sbc_cfg_.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
    sbc_cfg_.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
    sbc_cfg_.max_bitpool = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
    // Bluetooth spec definition -> SBC encoder definition
    btstack_sbc_allocation_method_t allocation =
        (btstack_sbc_allocation_method_t)(a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet) - 1);
    btstack_sbc_channel_mode_t mode = SBC_CHANNEL_MODE_STEREO;
    switch (a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet)) {
      case AVDTP_CHANNEL_MODE_JOINT_STEREO: mode = SBC_CHANNEL_MODE_JOINT_STEREO; break;
      case AVDTP_CHANNEL_MODE_STEREO: mode = SBC_CHANNEL_MODE_STEREO; break;
      case AVDTP_CHANNEL_MODE_DUAL_CHANNEL: mode = SBC_CHANNEL_MODE_DUAL_CHANNEL; break;
      case AVDTP_CHANNEL_MODE_MONO: mode = SBC_CHANNEL_MODE_MONO; break;
      default: break;
    }
    encoder_ = btstack_sbc_encoder_bluedroid_init_instance(&encoder_ctx_);
    encoder_->configure(&encoder_ctx_, SBC_MODE_STANDARD, sbc_cfg_.block_length, sbc_cfg_.subbands,
                        allocation, sbc_cfg_.sampling_frequency, sbc_cfg_.max_bitpool, mode);
    // sbc_buffer_length() is only valid after the first frame has been encoded: prime the
    // encoder with one frame of silence (the output is discarded)
    memset(pcm_, 0, sizeof(pcm_));
    encoder_->encode_signed_16(&encoder_ctx_, pcm_, media_.storage + 1);
  }

  // ---- media path ------------------------------------------------------------------------
  void close_codec() {
    if (active_codec_) active_codec_->close();
    active_codec_ = nullptr;
    free(codec_pcm_);
    codec_pcm_ = nullptr;
  }

  void timer_start() {
    if (active_codec_) {
      // open first: the packet size may depend on the stream parameters
      if (!active_codec_->open()) return;
      if (!codec_pcm_) codec_pcm_ = (int16_t*)malloc(active_codec_->frames_per_packet() * 4);
      if (!codec_pcm_) return;
    } else if (!encoder_) {
      return;
    }
    media_.max_payload = a2dp_max_media_payload_size(a2dp_cid_, media_.local_seid);
    if (media_.max_payload > kStorageSize) media_.max_payload = kStorageSize;
    media_.storage_count = 0;
    media_.ready_to_send = 0;
    media_.streaming = 1;
    media_.time_sent = 0;
    media_.missed = 0;
    media_.samples_ready = 0;
    btstack_run_loop_remove_timer(&media_.timer);
    btstack_run_loop_set_timer_handler(&media_.timer, &timer_cb);
    btstack_run_loop_set_timer(&media_.timer, kAudioTimeoutMs);
    btstack_run_loop_add_timer(&media_.timer);
  }

  void timer_stop() {
    media_.streaming = 0;
    media_.storage_count = 0;
    media_.ready_to_send = 0;
    btstack_run_loop_remove_timer(&media_.timer);
  }

  static void timer_cb(btstack_timer_source_t* ts) {
    (void)ts;
    BluetoothA2DPSource* self = static_cast<BluetoothA2DPSource*>(active());
    if (self) self->handle_timer();
  }

  void handle_timer() {
    if (!media_.streaming || (!encoder_ && !active_codec_)) return;
    btstack_run_loop_set_timer(&media_.timer, kAudioTimeoutMs);
    btstack_run_loop_add_timer(&media_.timer);

    // number of samples which have to be produced since the last call
    uint32_t now = btstack_run_loop_get_time_ms();
    uint32_t period = kAudioTimeoutMs;
    if (media_.time_sent > 0) period = now - media_.time_sent;
    uint32_t rate = active_codec_ ? active_codec_->sample_rate() : sbc_cfg_.sampling_frequency;
    uint32_t samples = (period * rate) / 1000;
    media_.missed += (period * rate) % 1000;
    while (media_.missed >= 1000) {
      samples++;
      media_.missed -= 1000;
    }
    media_.time_sent = now;
    media_.samples_ready += samples;

    if (media_.ready_to_send) return;
    if (active_codec_) {
      fill_codec_packet();
      return;
    }
    fill_sbc_buffer();

    if ((media_.storage_count + encoder_->sbc_buffer_length(&encoder_ctx_)) > media_.max_payload) {
      media_.ready_to_send = 1;
      a2dp_source_stream_endpoint_request_can_send_now(a2dp_cid_, media_.local_seid);
    }
  }

  /// Optional codec: one encoded frame per media packet
  void fill_codec_packet() {
    uint16_t n = active_codec_->frames_per_packet();
    // never build up a backlog if the encoder is slower than real time
    if (media_.samples_ready > 4u * n) media_.samples_ready = 4u * n;
    while (media_.samples_ready >= n) {
      fetch_pcm(codec_pcm_, n);
      media_.samples_ready -= n;
      uint16_t len = active_codec_->encode(codec_pcm_, media_.storage, (uint16_t)media_.max_payload);
      if (len) {
        media_.storage_count = len;
        media_.ready_to_send = 1;
        a2dp_source_stream_endpoint_request_can_send_now(a2dp_cid_, media_.local_seid);
        return;
      }
    }
  }

  void fill_sbc_buffer() {
    uint32_t frames_per_sbc = encoder_->num_audio_frames(&encoder_ctx_);
    uint16_t sbc_len = encoder_->sbc_buffer_length(&encoder_ctx_);
    while (media_.samples_ready >= frames_per_sbc &&
           (media_.max_payload - media_.storage_count) >= sbc_len) {
      fetch_pcm(pcm_, frames_per_sbc);
      // byte 0 of the storage is the SBC media header
      encoder_->encode_signed_16(&encoder_ctx_, pcm_, &media_.storage[1 + media_.storage_count]);
      media_.storage_count += sbc_len;
      media_.samples_ready -= frames_per_sbc;
    }
  }

  /// Gets 'frames' stereo frames from the application (silence if not available)
  void fetch_pcm(int16_t* out, uint32_t frames) {
    int32_t got = 0;
    if (frame_cb_) {
      got = frame_cb_((Frame*)out, (int32_t)frames);
    } else if (raw_cb_) {
      got = raw_cb_((uint8_t*)out, (int32_t)(frames * 4)) / 4;
    }
    if (got < 0) got = 0;
    if ((uint32_t)got > frames) got = frames;
    if ((uint32_t)got < frames) memset(out + got * 2, 0, (frames - got) * 4);
    if (software_volume_ && volume_ < 127) {
      int32_t gain = (volume_ * 32768) / 127;
      for (int32_t i = 0; i < got * 2; i++) out[i] = (int16_t)((out[i] * gain) >> 15);
    }
  }

  void send_media_packet() {
    if (active_codec_) {
      if (!media_.streaming || !media_.ready_to_send) return;
      if (active_codec_->uses_rtp()) {
        a2dp_source_stream_send_media_payload_rtp(a2dp_cid_, media_.local_seid, 0, media_.rtp_timestamp,
                                                  media_.storage, media_.storage_count);
      } else {
        a2dp_source_stream_send_media_packet(a2dp_cid_, media_.local_seid, media_.storage,
                                             media_.storage_count);
      }
      media_.rtp_timestamp += active_codec_->frames_per_packet();
      media_.storage_count = 0;
      media_.ready_to_send = 0;
      return;
    }
    if (!media_.streaming || !encoder_) return;
    uint16_t sbc_len = encoder_->sbc_buffer_length(&encoder_ctx_);
    uint16_t bytes = media_.storage_count;
    uint8_t num_frames = bytes / sbc_len;
    media_.storage[0] = num_frames;  // no fragmentation
    a2dp_source_stream_send_media_payload_rtp(a2dp_cid_, media_.local_seid, 0, media_.rtp_timestamp,
                                              media_.storage, bytes + 1);
    media_.rtp_timestamp += num_frames * encoder_->num_audio_frames(&encoder_ctx_);
    media_.storage_count = 0;
    media_.ready_to_send = 0;
  }

  struct Media {
    uint8_t local_seid;
    int max_payload;
    uint8_t streaming;
    uint8_t ready_to_send;
    uint32_t time_sent;
    uint32_t missed;
    uint32_t samples_ready;
    uint32_t rtp_timestamp;
    uint16_t storage_count;
    uint8_t storage[kStorageSize];
    btstack_timer_source_t timer;
  };
  struct SbcConfig {
    uint16_t sampling_frequency;
    uint8_t num_channels, block_length, subbands, max_bitpool;
  };
  struct Pending {
    bd_addr_t addr;
    uint8_t psrm;
    uint16_t clock_offset;
    int rssi;
  };

  uint8_t caps_[4];
  uint8_t cfg_[4];
  uint8_t sdp_source_[150];

  Media media_;
  SbcConfig sbc_cfg_;
  static const int kMaxCodecs = 2;
  SourceCodec* codecs_[kMaxCodecs] = {nullptr, nullptr};
  int codec_count_ = 0;
  SourceCodec* sbc_codec_ = nullptr;  // replaces the built-in SBC encoder
  SourceCodec* active_codec_ = nullptr;
  int16_t* codec_pcm_ = nullptr;

  const btstack_sbc_encoder_t* encoder_ = nullptr;
  btstack_sbc_encoder_bluedroid_t encoder_ctx_;
  int16_t pcm_[256 * 2];

  FrameDataCallback frame_cb_ = nullptr;
  RawDataCallback raw_cb_ = nullptr;
  DeviceFoundCallback device_cb_ = nullptr;
  bool software_volume_ = true;

  char want_name_[40] = {0};
  char np_title_[64] = {0};
  char np_artist_[64] = {0};
  char np_album_[64] = {0};
  bool scan_ = false;
  bool connect_on_start_ = false;
  bool requesting_name_ = false;
  Pending pending_[kMaxPending];
  int pending_count_ = 0;
  Pending current_;
};

}  // namespace blue_a2dp

typedef blue_a2dp::BluetoothA2DPSource BluetoothA2DPSource;
