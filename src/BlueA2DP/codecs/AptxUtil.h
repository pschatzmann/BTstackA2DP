// aptX in A2DP is a vendor codec ("non-A2DP", media codec type 0xFF): the codec information
// is 4 bytes vendor id (0x0000004F, APT Ltd) + 2 bytes codec id (0x0001, little endian)
// + 1 byte with the sampling rate (high nibble) and channel mode (low nibble).
// No dependencies.
#pragma once

#include <stdint.h>
#include <string.h>

namespace blue_a2dp {
namespace aptx {

static const uint8_t kInfoLen = 7;
static const uint8_t kFreq16000 = 0x80, kFreq32000 = 0x40, kFreq44100 = 0x20, kFreq48000 = 0x10;
static const uint8_t kMono = 0x01, kStereo = 0x02;

/// true if the codec information describes aptX (vendor + codec id)
inline bool is_aptx(const uint8_t* info, uint8_t len) {
  static const uint8_t id[6] = {0x4F, 0x00, 0x00, 0x00, 0x01, 0x00};
  return len >= kInfoLen && memcmp(info, id, sizeof(id)) == 0;
}

/// Fills the 7 byte codec information
inline void make_info(uint8_t* info, uint8_t freq_and_channels) {
  static const uint8_t id[6] = {0x4F, 0x00, 0x00, 0x00, 0x01, 0x00};
  memcpy(info, id, sizeof(id));
  info[6] = freq_and_channels;
}

inline uint32_t sample_rate(uint8_t b) {
  switch (b & 0xF0) {
    case kFreq16000: return 16000;
    case kFreq32000: return 32000;
    case kFreq44100: return 44100;
    case kFreq48000: return 48000;
    default: return 0;
  }
}

inline int channels(uint8_t b) { return (b & kMono) ? 1 : 2; }

/// Picks 44.1 kHz (preferred) or 48 kHz stereo from the capability byte of the remote
/// device. Returns the configuration byte or 0 if stereo at these rates is not offered.
inline uint8_t choose(uint8_t capability) {
  if (!(capability & kStereo)) return 0;
  if (capability & kFreq44100) return kFreq44100 | kStereo;
  if (capability & kFreq48000) return kFreq48000 | kStereo;
  return 0;
}

}  // namespace aptx
}  // namespace blue_a2dp
