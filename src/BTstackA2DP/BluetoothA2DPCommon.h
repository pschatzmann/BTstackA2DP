// Shared functionality of the A2DP sink and source: btstack setup (L2CAP, SDP, AVRCP,
// GAP), pairing, connection/audio state tracking, AVRCP controller/target handling and
// the thread-safe command interface.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// btstack_config.h has to come first: it selects features and buffer sizes
#include "btstack_config.h"

#include "A2DPTypes.h"
#include "ports/Port.h"

#include <btstack/bluetooth_company_id.h>
#include <btstack/bluetooth_sdp.h>
#include <btstack/btstack_defines.h>
#include <btstack/btstack_event.h>
#include <btstack/btstack_run_loop.h>
#include <btstack/btstack_util.h>
#include <btstack/classic/a2dp.h>
#include <btstack/classic/a2dp_sink.h>
#include <btstack/classic/a2dp_source.h>
#include <btstack/classic/avdtp.h>
#include <btstack/classic/avdtp_sink.h>
#include <btstack/classic/avdtp_source.h>
#include <btstack/classic/avdtp_util.h>
#include <btstack/classic/avrcp.h>
#include <btstack/classic/avrcp_controller.h>
#include <btstack/classic/avrcp_target.h>
#include <btstack/classic/btstack_sbc.h>
#include <btstack/classic/device_id_server.h>
#include <btstack/classic/sdp_server.h>
#include <btstack/gap.h>
#include <btstack/hci.h>
#include <btstack/l2cap.h>

// Define BTSTACKA2DP_DEBUG to get a log of the connection handling on stdout / Serial
#ifdef BTSTACKA2DP_DEBUG
#define BTSTACKA2DP_LOG(...)         \
  do {                            \
    printf("[BTstackA2DP] " __VA_ARGS__); \
    printf("\n");                 \
  } while (0)
#else
#define BTSTACKA2DP_LOG(...) \
  do {                    \
  } while (0)
#endif

#ifndef BTSTACKA2DP_VOLUME_STEP
#define BTSTACKA2DP_VOLUME_STEP 8
#endif

namespace blue_a2dp {

class BluetoothA2DPCommon {
 public:
  BluetoothA2DPCommon() {
    memset(name_, 0, sizeof(name_));
    strncpy(name_, "BTstackA2DP", sizeof(name_) - 1);
    memset(peer_, 0, sizeof(peer_));
    memset(pin_, 0, sizeof(pin_));
    strcpy(pin_, "0000");
  }
  virtual ~BluetoothA2DPCommon() {}

  // ---- configuration (call before start) -----------------------------------------
  /// Reconnect to the last device when the link is lost / at startup
  void set_auto_reconnect(bool active) { auto_reconnect_ = active; }
  bool get_auto_reconnect() const { return auto_reconnect_; }

  void set_on_connection_state_changed(ConnectionStateCallback cb, void* obj = nullptr) {
    connection_cb_ = cb;
    connection_obj_ = obj;
  }
  void set_on_audio_state_changed(AudioStateCallback cb, void* obj = nullptr) {
    audio_state_cb_ = cb;
    audio_state_obj_ = obj;
  }
  void set_avrc_metadata_callback(MetadataCallback cb) { metadata_cb_ = cb; }
  void set_avrc_rn_playstatus_callback(PlayStatusCallback cb) { playstatus_cb_ = cb; }
  void set_on_volume_change(VolumeCallback cb) { volume_cb_ = cb; }
  /// Called for every AVRCP key press / release from the remote device (also next, previous)
  void set_on_avrcp_operation(OperationCallback cb, void* obj = nullptr) {
    operation_cb_ = cb;
    operation_obj_ = obj;
  }
  /// Return false to reject an SSP pairing request. Default: accept.
  void set_pairing_confirm_callback(PairingConfirmCallback cb) { pairing_cb_ = cb; }
  /// PIN used for legacy pairing (old devices). Default "0000".
  void set_pin_code(const char* pin) { strncpy(pin_, pin, sizeof(pin_) - 1); }
  /// Name under which this device is visible
  void set_name(const char* name) {
    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = 0;
  }
  const char* get_name() const { return name_; }

  // ---- state ------------------------------------------------------------------------
  ConnectionState get_connection_state() const { return connection_state_; }
  AudioState get_audio_state() const { return audio_state_; }
  bool is_connected() const { return connection_state_ == ConnectionState::Connected; }
  /// true once the Bluetooth controller is up and running (start() returns before that, e.g.
  /// if no controller can be found on the desktop)
  bool is_ready() const { return ready_; }
  /// Address of the connected (or last connected) peer
  const uint8_t* get_peer_address() const { return peer_; }
  /// Absolute volume 0..127
  int get_volume() const { return volume_; }

  // ---- commands (thread safe) -------------------------------------------------------
  /// Sets the absolute volume (0..127) and informs the peer
  void set_volume(uint8_t volume) {
    if (volume > 127) volume = 127;
    volume_ = volume;
    run<&BluetoothA2DPCommon::do_send_volume>();
  }
  void play() { run<&BluetoothA2DPCommon::do_play>(); }
  void pause() { run<&BluetoothA2DPCommon::do_pause>(); }
  void stop() { run<&BluetoothA2DPCommon::do_stop>(); }
  void next() { run<&BluetoothA2DPCommon::do_next>(); }
  void previous() { run<&BluetoothA2DPCommon::do_previous>(); }
  void fast_forward() { run<&BluetoothA2DPCommon::do_fast_forward>(); }
  void rewind() { run<&BluetoothA2DPCommon::do_rewind>(); }
  /// Close the connection to the peer
  void disconnect() { run<&BluetoothA2DPCommon::do_disconnect>(); }
  /// Connect to the last peer
  void reconnect() { run<&BluetoothA2DPCommon::do_reconnect>(); }
  /// Power down the controller
  void end() {
    if (started_) Port::stop();
    started_ = false;
  }
  /// Remove all stored pairing information
  void clean_last_connection() { run<&BluetoothA2DPCommon::do_delete_link_keys>(); }

 protected:
  // ---- role specific hooks (called in the btstack context) --------------------------
  virtual bool is_sink() const = 0;
  /// Role specific initialization: create endpoint, SDP record, register handlers
  virtual bool setup_role() = 0;
  virtual void on_hci_event(uint8_t* packet) { (void)packet; }
  virtual void on_hci_working() {}
  virtual void on_avrcp_connected() {}
  virtual void on_avrcp_operation(avrcp_operation_id_t op, bool pressed) {
    if (!pressed) return;
    if (op == AVRCP_OPERATION_ID_VOLUME_UP) change_volume(BTSTACKA2DP_VOLUME_STEP);
    if (op == AVRCP_OPERATION_ID_VOLUME_DOWN) change_volume(-BTSTACKA2DP_VOLUME_STEP);
  }
  virtual void do_play() {
    if (avrcp_cid_) avrcp_controller_play(avrcp_cid_);
  }
  virtual void do_pause() {
    if (avrcp_cid_) avrcp_controller_pause(avrcp_cid_);
  }
  virtual void do_stop() {
    if (avrcp_cid_) avrcp_controller_stop(avrcp_cid_);
  }
  virtual void on_avrcp_target_event(uint8_t* packet) { (void)packet; }
  virtual void do_connect_to_peer() {}
  virtual void do_disconnect_peer() {}
  virtual void do_send_volume() {}
  virtual uint32_t class_of_device() const = 0;

  // ---- start / common init -----------------------------------------------------------
  bool start_stack() {
    if (started_) return true;
    active() = this;
    if (!Port::start(&setup_thunk, this)) return false;
    started_ = true;
    return true;
  }

  static BluetoothA2DPCommon*& active() {
    static BluetoothA2DPCommon* p = nullptr;
    return p;
  }

  void set_connection_state(ConnectionState s) {
    if (s == ConnectionState::Connected) {
      user_disconnect_ = false;
      reconnect_attempts_ = 0;
      key_dropped_ = false;
    }
    if (connection_state_ == s) return;
    connection_state_ = s;
    if (connection_cb_) connection_cb_(s, connection_obj_);
  }
  void set_audio_state(AudioState s) {
    if (audio_state_ == s) return;
    audio_state_ = s;
    if (audio_state_cb_) audio_state_cb_(s, audio_state_obj_);
  }

  void change_volume(int delta) {
    int v = volume_ + delta;
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    volume_ = v;
    do_send_volume();
    if (volume_cb_) volume_cb_(volume_);
  }

  /// Called by the roles when the link was lost or a connection attempt failed: try again later.
  /// Without delay the waiting time grows with every failed attempt (2, 4, 8, 16, 32, then
  /// 60 seconds) and starts again after a successful connection.
  void schedule_reconnect(uint32_t delay_ms = 0) {
    if (!auto_reconnect_ || !have_peer_ || user_disconnect_) return;
    if (delay_ms == 0) {
      delay_ms = 2000u << (reconnect_attempts_ < 5 ? reconnect_attempts_ : 5);
      if (delay_ms > 60000) delay_ms = 60000;
      reconnect_attempts_++;
    }
    BTSTACKA2DP_LOG("reconnect in %u ms", (unsigned)delay_ms);
    btstack_run_loop_remove_timer(&reconnect_timer_);
    btstack_run_loop_set_timer_handler(&reconnect_timer_, &reconnect_timer_cb);
    btstack_run_loop_set_timer(&reconnect_timer_, delay_ms);
    btstack_run_loop_add_timer(&reconnect_timer_);
  }

  /// Called by the roles when the A2DP signaling connection could not be established.
  /// If the remote device lost its bond (it was reset or "forgot" us) our stored link key is
  /// useless and every retry would fail the same way: drop it, so the next attempt pairs again.
  void handle_connection_failure(uint8_t status, bd_addr_t addr) {
    BTSTACKA2DP_LOG("signaling connection to %s failed, status 0x%02x", bd_addr_to_str(addr), status);
    if (status == L2CAP_CONNECTION_PIN_OR_LINK_KEY_MISSING || status == ERROR_CODE_PIN_OR_KEY_MISSING ||
        status == ERROR_CODE_AUTHENTICATION_FAILURE) {
      BTSTACKA2DP_LOG("link key rejected by %s: deleted, pairing again", bd_addr_to_str(addr));
      gap_drop_link_key_for_bd_addr(addr);
      if (!key_dropped_) {
        key_dropped_ = true;  // once per connection: a failing new pairing uses the normal backoff
        schedule_reconnect(1000);
        return;
      }
    }
    schedule_reconnect();
  }

  /// No automatic reconnects until the next successful connection (e.g. incompatible remote)
  void stop_reconnecting() { user_disconnect_ = true; }

  void remote_volume_changed(int v) {
    volume_ = v;
    if (volume_cb_) volume_cb_(v);
  }

  /// Run a member function in the btstack context
  template <void (BluetoothA2DPCommon::*M)()>
  void run() {
    if (!started_) return;
    Port::post(&thunk<M>, this);
  }

  // ---- state shared with the roles -------------------------------------------------
  char name_[40];
  char pin_[17];
  bool started_ = false;
  volatile bool ready_ = false;
  bool auto_reconnect_ = true;
  bd_addr_t peer_;
  bool have_peer_ = false;
  uint16_t a2dp_cid_ = 0;
  uint16_t avrcp_cid_ = 0;
  int volume_ = 127;
  ConnectionState connection_state_ = ConnectionState::Disconnected;
  AudioState audio_state_ = AudioState::Stopped;
  ConnectionStateCallback connection_cb_ = nullptr;
  void* connection_obj_ = nullptr;
  AudioStateCallback audio_state_cb_ = nullptr;
  void* audio_state_obj_ = nullptr;
  MetadataCallback metadata_cb_ = nullptr;
  PlayStatusCallback playstatus_cb_ = nullptr;
  OperationCallback operation_cb_ = nullptr;
  void* operation_obj_ = nullptr;
  VolumeCallback volume_cb_ = nullptr;
  PairingConfirmCallback pairing_cb_ = nullptr;

 private:
  template <void (BluetoothA2DPCommon::*M)()>
  static void thunk(void* p) {
    (static_cast<BluetoothA2DPCommon*>(p)->*M)();
  }

  static bool setup_thunk(void* p) { return static_cast<BluetoothA2DPCommon*>(p)->setup_all(); }

  // ---- AVRCP commands -----------------------------------------------------------------
  void do_next() {
    if (avrcp_cid_) avrcp_controller_forward(avrcp_cid_);
  }
  void do_previous() {
    if (avrcp_cid_) avrcp_controller_backward(avrcp_cid_);
  }
  void do_fast_forward() {
    if (avrcp_cid_) avrcp_controller_fast_forward(avrcp_cid_);
  }
  void do_rewind() {
    if (avrcp_cid_) avrcp_controller_rewind(avrcp_cid_);
  }
  void do_disconnect() {
    user_disconnect_ = true;
    btstack_run_loop_remove_timer(&reconnect_timer_);
    do_disconnect_peer();
  }
  void do_reconnect() {
    user_disconnect_ = false;
    if (have_peer_ && connection_state_ == ConnectionState::Disconnected) do_connect_to_peer();
  }
  void do_delete_link_keys() { gap_delete_all_link_keys(); }

  // ---- initialization ----------------------------------------------------------------
  bool setup_all() {
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
    l2cap_init();
    sdp_init();

    if (!setup_role()) return false;

    avrcp_init();
    avrcp_controller_init();
    avrcp_target_init();
    avrcp_register_packet_handler(&avrcp_cb);
    avrcp_controller_register_packet_handler(&avrcp_controller_cb);
    avrcp_target_register_packet_handler(&avrcp_target_cb);

    memset(sdp_avrcp_controller_, 0, sizeof(sdp_avrcp_controller_));
    avrcp_controller_create_sdp_record(
        sdp_avrcp_controller_, sdp_create_service_record_handle(),
        is_sink() ? (1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER)
                  : (1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER),
        nullptr, nullptr);
    sdp_register_service(sdp_avrcp_controller_);

    memset(sdp_avrcp_target_, 0, sizeof(sdp_avrcp_target_));
    avrcp_target_create_sdp_record(
        sdp_avrcp_target_, sdp_create_service_record_handle(),
        is_sink() ? (1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER)
                  : (1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER),
        nullptr, nullptr);
    sdp_register_service(sdp_avrcp_target_);

    memset(sdp_device_id_, 0, sizeof(sdp_device_id_));
    device_id_create_sdp_record(sdp_device_id_, sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    sdp_register_service(sdp_device_id_);

    gap_set_local_name(name_);
    gap_discoverable_control(1);
    gap_set_class_of_device(class_of_device());
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
                                         LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);
    gap_ssp_set_auto_accept(pairing_cb_ == nullptr);

    hci_event_registration_.callback = &hci_cb;
    hci_add_event_handler(&hci_event_registration_);
    return true;
  }

  // ---- event handlers (static -> instance) -----------------------------------------
  static void hci_cb(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    BluetoothA2DPCommon* self = active();
    if (type != HCI_EVENT_PACKET || !self) return;
    self->handle_hci(packet);
  }

  void handle_hci(uint8_t* packet) {
    bd_addr_t addr;
    switch (hci_event_packet_get_type(packet)) {
      case BTSTACK_EVENT_STATE:
        BTSTACKA2DP_LOG("hci state %d", btstack_event_state_get_state(packet));
        ready_ = btstack_event_state_get_state(packet) == HCI_STATE_WORKING;
        if (ready_) on_hci_working();
        break;
      case HCI_EVENT_PIN_CODE_REQUEST:
        hci_event_pin_code_request_get_bd_addr(packet, addr);
        gap_pin_code_response(addr, pin_);
        break;
      case HCI_EVENT_USER_CONFIRMATION_REQUEST:
        hci_event_user_confirmation_request_get_bd_addr(packet, addr);
        if (!pairing_cb_ ||
            pairing_cb_(hci_event_user_confirmation_request_get_numeric_value(packet)))
          gap_ssp_confirmation_response(addr);
        else
          gap_ssp_confirmation_negative(addr);
        break;
      default:
        break;
    }
    on_hci_event(packet);
  }

  static void avrcp_cb(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    BluetoothA2DPCommon* self = active();
    if (type != HCI_EVENT_PACKET || !self) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    switch (packet[2]) {
      case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED:
        if (avrcp_subevent_connection_established_get_status(packet) != ERROR_CODE_SUCCESS) return;
        self->avrcp_cid_ = avrcp_subevent_connection_established_get_avrcp_cid(packet);
        BTSTACKA2DP_LOG("avrcp connected, cid 0x%04x", self->avrcp_cid_);
        self->on_avrcp_connected();
        break;
      case AVRCP_SUBEVENT_CONNECTION_RELEASED:
        self->avrcp_cid_ = 0;
        self->peer_events_ = 0;
        break;
      default:
        break;
    }
  }

  static void avrcp_controller_cb(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    BluetoothA2DPCommon* self = active();
    if (type != HCI_EVENT_PACKET || !self || !self->avrcp_cid_) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    self->handle_avrcp_controller(packet);
  }

  void emit_text(uint8_t id, const uint8_t* value, uint16_t len) {
    if (!metadata_cb_) return;
    char text[256];
    if (len > sizeof(text) - 1) len = sizeof(text) - 1;
    if (len) memcpy(text, value, len);
    text[len] = 0;
    metadata_cb_(id, (const uint8_t*)text);
  }

  void emit_number(uint8_t id, uint32_t value) {
    if (!metadata_cb_) return;
    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    metadata_cb_(id, (const uint8_t*)text);
  }

  void handle_avrcp_controller(uint8_t* packet) {
    switch (packet[2]) {
      case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID:
        peer_events_ |= (1u << avrcp_subevent_get_capability_event_id_get_event_id(packet));
        break;
      case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID_DONE:
        if (peer_events_ & (1u << AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED))
          avrcp_controller_enable_notification(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
        if (peer_events_ & (1u << AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED))
          avrcp_controller_enable_notification(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
        if (peer_events_ & (1u << AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED))
          avrcp_controller_enable_notification(avrcp_cid_, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
        avrcp_controller_get_now_playing_info(avrcp_cid_);
        break;
      case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
        if (playstatus_cb_)
          playstatus_cb_((PlayStatus)avrcp_subevent_notification_playback_status_changed_get_play_status(packet));
        break;
      case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
      case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:
        avrcp_controller_get_now_playing_info(avrcp_cid_);
        break;
      case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
        remote_volume_changed(avrcp_subevent_notification_volume_changed_get_absolute_volume(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
        emit_text(MD_TITLE, avrcp_subevent_now_playing_title_info_get_value(packet),
                  avrcp_subevent_now_playing_title_info_get_value_len(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
        emit_text(MD_ARTIST, avrcp_subevent_now_playing_artist_info_get_value(packet),
                  avrcp_subevent_now_playing_artist_info_get_value_len(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:
        emit_text(MD_ALBUM, avrcp_subevent_now_playing_album_info_get_value(packet),
                  avrcp_subevent_now_playing_album_info_get_value_len(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:
        emit_text(MD_GENRE, avrcp_subevent_now_playing_genre_info_get_value(packet),
                  avrcp_subevent_now_playing_genre_info_get_value_len(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:
        emit_number(MD_TRACK_NUM, avrcp_subevent_now_playing_track_info_get_track(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_TOTAL_TRACKS_INFO:
        emit_number(MD_NUM_TRACKS, avrcp_subevent_now_playing_total_tracks_info_get_total_tracks(packet));
        break;
      case AVRCP_SUBEVENT_NOW_PLAYING_SONG_LENGTH_MS_INFO:
        emit_number(MD_PLAYING_TIME, avrcp_subevent_now_playing_song_length_ms_info_get_song_length(packet));
        break;
      default:
        break;
    }
  }

  static void avrcp_target_cb(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;
    BluetoothA2DPCommon* self = active();
    if (type != HCI_EVENT_PACKET || !self) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    switch (packet[2]) {
      case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
        self->remote_volume_changed(avrcp_subevent_notification_volume_changed_get_absolute_volume(packet));
        break;
      case AVRCP_SUBEVENT_OPERATION:
        {
          uint8_t op = avrcp_subevent_operation_get_operation_id(packet);
          bool pressed = avrcp_subevent_operation_get_button_pressed(packet) > 0;
          if (self->operation_cb_) self->operation_cb_(op, pressed, self->operation_obj_);
          self->on_avrcp_operation((avrcp_operation_id_t)op, pressed);
        }
        break;
      default:
        self->on_avrcp_target_event(packet);
        break;
    }
  }

  static void reconnect_timer_cb(btstack_timer_source_t*) {
    BluetoothA2DPCommon* self = active();
    if (self && self->connection_state_ == ConnectionState::Disconnected && !self->user_disconnect_)
      self->do_connect_to_peer();
  }

  btstack_timer_source_t reconnect_timer_;
  bool user_disconnect_ = false;
  uint8_t reconnect_attempts_ = 0;
  bool key_dropped_ = false;  // stale link key already deleted in this reconnect cycle
  uint8_t sdp_avrcp_controller_[200];
  uint8_t sdp_avrcp_target_[200];
  uint8_t sdp_device_id_[100];
  btstack_packet_callback_registration_t hci_event_registration_;
  uint32_t peer_events_ = 0;
};

}  // namespace blue_a2dp
