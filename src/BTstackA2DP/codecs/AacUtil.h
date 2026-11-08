// Helpers for A2DP AAC: the media payload is a LATM AudioMuxElement (MuxConfigPresent=0)
// while most AAC decoders (e.g. libhelix) expect ADTS. No dependencies.
#pragma once

#include <stdint.h>
#include <string.h>

namespace blue_a2dp {
namespace aac {

/// ADTS sampling_frequency_index for a sample rate in Hz, -1 if not supported
inline int sample_rate_index(uint32_t rate) {
  static const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000,
                                   24000, 22050, 16000, 12000, 11025, 8000};
  for (int i = 0; i < 12; i++)
    if (rates[i] == rate) return i;
  return -1;
}

/// Extracts the AAC frame from a LATM AudioMuxElement. Returns the frame length (0 on
/// error) and sets 'frame' to the first byte of the frame.
inline uint16_t latm_frame(const uint8_t* payload, uint16_t len, const uint8_t** frame) {
  uint32_t size = 0;
  uint16_t pos = 0;
  uint8_t b;
  do {  // PayloadLengthInfo: 0xFF bytes followed by the remainder
    if (pos >= len) return 0;
    b = payload[pos++];
    size += b;
  } while (b == 0xFF);
  if (size == 0 || pos + size > len) return 0;
  *frame = payload + pos;
  return (uint16_t)size;
}

/// Writes the 7 byte ADTS header for a frame of 'frame_len' bytes
/// object_type: 1 = AAC Main, 2 = AAC LC, ...
inline void adts_header(uint8_t* h, uint16_t frame_len, int sr_index, int channels,
                        int object_type = 2) {
  uint16_t total = frame_len + 7;
  h[0] = 0xFF;
  h[1] = 0xF1;  // MPEG-4, layer 0, no CRC
  h[2] = (uint8_t)(((object_type - 1) << 6) | (sr_index << 2) | ((channels >> 2) & 1));
  h[3] = (uint8_t)(((channels & 3) << 6) | ((total >> 11) & 3));
  h[4] = (uint8_t)((total >> 3) & 0xFF);
  h[5] = (uint8_t)(((total & 7) << 5) | 0x1F);
  h[6] = 0xFC;  // buffer fullness (VBR), 1 raw data block
}

/// Finds the first ADTS frame in 'buf'. Returns the raw AAC frame length (0 if there is no
/// complete frame), sets 'frame' to its first byte and 'consumed' to the ADTS frame size.
inline uint16_t adts_frame(const uint8_t* buf, uint16_t len, const uint8_t** frame,
                           uint16_t* consumed) {
  if (len < 7 || buf[0] != 0xFF || (buf[1] & 0xF0) != 0xF0) return 0;
  uint16_t total = (uint16_t)(((buf[3] & 3) << 11) | (buf[4] << 3) | (buf[5] >> 5));
  uint16_t header = (buf[1] & 1) ? 7 : 9;  // protection_absent == 0 -> 2 byte CRC
  if (total < header || total > len) return 0;
  *frame = buf + header;
  *consumed = total;
  return total - header;
}

/// Wraps a raw AAC frame into a LATM AudioMuxElement (MuxConfigPresent=0). Returns the
/// payload length, 0 if 'max_len' is too small.
inline uint16_t latm_wrap(const uint8_t* frame, uint16_t n, uint8_t* out, uint16_t max_len) {
  uint16_t pos = 0;
  uint16_t rest = n;
  uint16_t length_bytes = n / 255 + 1;
  if ((uint32_t)length_bytes + n > max_len) return 0;
  while (rest >= 255) {
    out[pos++] = 0xFF;
    rest -= 255;
  }
  out[pos++] = (uint8_t)rest;
  memcpy(out + pos, frame, n);
  return pos + n;
}

}  // namespace aac
}  // namespace blue_a2dp
