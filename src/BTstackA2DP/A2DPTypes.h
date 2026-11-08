// Common types for BTstackA2DP. The names intentionally mirror the ESP32-A2DP library
// (https://github.com/pschatzmann/ESP32-A2DP) so that existing sketches port easily.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace blue_a2dp {

/// Bluetooth device address (6 bytes, big endian / display order)
typedef uint8_t BDAddr[6];

enum class ConnectionState : uint8_t {
  Disconnected = 0,
  Connecting,
  Connected,
  Disconnecting
};

/// State of the audio stream
enum class AudioState : uint8_t {
  Suspended = 0,  // connected, but remote suspended / paused the stream
  Stopped,        // stream released
  Started         // audio is flowing
};

/// AVRCP metadata attribute ids (same values as ESP_AVRC_MD_ATTR_*)
enum MetadataId : uint8_t {
  MD_TITLE = 0x01,
  MD_ARTIST = 0x02,
  MD_ALBUM = 0x04,
  MD_TRACK_NUM = 0x08,
  MD_NUM_TRACKS = 0x10,
  MD_GENRE = 0x20,
  MD_PLAYING_TIME = 0x40
};

/// AVRCP play status
enum class PlayStatus : uint8_t {
  Stopped = 0,
  Playing = 1,
  Paused = 2,
  FwdSeek = 3,
  RevSeek = 4,
  Error = 0xFF
};

/// One stereo PCM frame (16 bit) - used by the source data callback
struct Frame {
  Frame(int v = 0) : channel1(v), channel2(v) {}
  Frame(int ch1, int ch2) : channel1(ch1), channel2(ch2) {}
  int16_t channel1;
  int16_t channel2;
};

/// Sample format of the PCM data delivered to / requested from the application
struct AudioInfo {
  uint32_t sample_rate = 44100;
  uint8_t channels = 2;
  uint8_t bits_per_sample = 16;
};

// ---- callback types ----------------------------------------------------------
// plain function pointers (no std::function) so that this works on small targets;
// 'obj' is the opaque pointer which was registered together with the callback.

typedef void (*ConnectionStateCallback)(ConnectionState state, void* obj);
typedef void (*AudioStateCallback)(AudioState state, void* obj);
/// Called when the sample rate / channels of the stream are known or change
typedef void (*AudioInfoCallback)(const AudioInfo& info, void* obj);
/// PCM data from the sink: interleaved 16 bit samples
typedef void (*StreamReaderCallback)(const uint8_t* data, uint32_t length);
/// Notification that a packet has been decoded and passed to the stream reader
typedef void (*DataReceivedCallback)();
/// Absolute volume 0..127
typedef void (*VolumeCallback)(int volume);
/// AVRCP metadata: text is a 0 terminated UTF-8 string
typedef void (*MetadataCallback)(uint8_t id, const uint8_t* text);
typedef void (*PlayStatusCallback)(PlayStatus status);
/// AVRCP operation received from the remote device (e.g. next, previous, play); op is the AVRCP
/// operation id (avrcp_operation_id_t), pressed is false on button release
typedef void (*OperationCallback)(uint8_t op, bool pressed, void* obj);
/// Source: fill 'frames' with up to 'count' frames; return the number of frames written
typedef int32_t (*FrameDataCallback)(Frame* frames, int32_t count);
/// Source: fill 'data' with up to 'len' bytes of 16 bit stereo PCM; return bytes written
typedef int32_t (*RawDataCallback)(uint8_t* data, int32_t len);
/// Source: inspect a discovered device; return true to connect to it
typedef bool (*DeviceFoundCallback)(const char* name, const uint8_t* addr, int rssi);
/// Called if the PIN/numeric comparison for pairing has to be confirmed (return true to accept)
typedef bool (*PairingConfirmCallback)(uint32_t numeric_value);

}  // namespace blue_a2dp
