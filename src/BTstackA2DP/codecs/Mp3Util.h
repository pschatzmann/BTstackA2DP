// Helpers for the A2DP "MPEG-1,2 Audio" codec (MP3). No dependencies.
#pragma once

#include <stdint.h>

namespace blue_a2dp {
namespace mp3 {

/// Size of the RFC 2250 payload header (MBZ + fragmentation offset) in front of the MP3 data
static const uint8_t kPayloadHeaderLen = 4;

/// Bit rate of the A2DP bit rate index (MPEG-1 layer III) in kbit/s, 0 if invalid / free format
inline int bit_rate_kbps(int index) {
  static const uint16_t rates[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
  return (index >= 0 && index < 15) ? rates[index] : 0;
}

/// Picks the bit rate index closest to 128 kbit/s that the remote device offers
/// (bitmap: bit n <-> index n). Returns -1 if none.
inline int choose_bit_rate_index(uint16_t bitmap) {
  static const int preference[] = {9, 8, 10, 7, 11, 6, 12, 5};  // 128, 112, 160, 96, 192, ...
  for (unsigned i = 0; i < sizeof(preference) / sizeof(preference[0]); i++)
    if (bitmap & (1u << preference[i])) return preference[i];
  return -1;
}

/// Picks the sampling rate (44.1 kHz preferred) from the remote bitmap
/// (bit0 = 48000, bit1 = 44100, bit2 = 32000, ...). Returns 0 if none of the two is offered.
inline uint32_t choose_sampling_frequency(uint8_t bitmap) {
  if (bitmap & 0x02) return 44100;
  if (bitmap & 0x01) return 48000;
  return 0;
}

}  // namespace mp3
}  // namespace blue_a2dp
