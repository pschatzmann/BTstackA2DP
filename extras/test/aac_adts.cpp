// Hardware free test of the LATM -> ADTS conversion used by the A2DP AAC codec.
#include <stdio.h>
#include <string.h>
#include "BTstackA2DP/codecs/AacUtil.h"
using namespace blue_a2dp::aac;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int test_aac() {
  CHECK(sample_rate_index(44100) == 4 && sample_rate_index(48000) == 3 && sample_rate_index(1234) == -1);

  // short frame: length byte + 5 data bytes
  uint8_t latm[] = {5, 1, 2, 3, 4, 5};
  const uint8_t* f = nullptr;
  CHECK(latm_frame(latm, sizeof(latm), &f) == 5 && f == latm + 1);
  // long frame: 0xFF + 0x2D = 255 + 45 = 300 bytes
  uint8_t big[2 + 300];
  big[0] = 0xFF; big[1] = 45;
  CHECK(latm_frame(big, sizeof(big), &f) == 300 && f == big + 2);
  // truncated / empty
  CHECK(latm_frame(big, 100, &f) == 0);
  CHECK(latm_frame(latm, 0, &f) == 0);

  // ADTS header for 44.1 kHz stereo LC, 300 byte frame -> 307 byte packet
  uint8_t h[7];
  adts_header(h, 300, sample_rate_index(44100), 2);
  CHECK(h[0] == 0xFF && (h[1] & 0xF6) == 0xF0);                // syncword, layer 0
  CHECK(((h[2] >> 6) & 3) == 1);                                // profile = LC - 1
  CHECK(((h[2] >> 2) & 0xF) == 4);                              // 44.1 kHz
  CHECK((((h[2] & 1) << 2) | (h[3] >> 6)) == 2);                // channels
  CHECK(((((h[3] & 3) << 11) | (h[4] << 3) | (h[5] >> 5))) == 307);  // frame length

  // round trip: raw frame -> ADTS -> adts_frame -> LATM -> latm_frame
  uint8_t raw[300];
  for (int i = 0; i < 300; i++) raw[i] = (uint8_t)i;
  uint8_t adts[7 + 300];
  adts_header(adts, 300, 4, 2);
  memcpy(adts + 7, raw, 300);
  const uint8_t* fr = nullptr;
  uint16_t used = 0;
  CHECK(adts_frame(adts, sizeof(adts), &fr, &used) == 300 && used == 307 && fr == adts + 7);
  CHECK(adts_frame(adts, 100, &fr, &used) == 0);  // incomplete
  uint8_t latm_out[400];
  uint16_t ln = latm_wrap(raw, 300, latm_out, sizeof(latm_out));
  CHECK(ln == 302 && latm_out[0] == 0xFF && latm_out[1] == 45);
  const uint8_t* back = nullptr;
  CHECK(latm_frame(latm_out, ln, &back) == 300 && memcmp(back, raw, 300) == 0);
  CHECK(latm_wrap(raw, 300, latm_out, 100) == 0);  // too small
  // exactly 255 bytes needs an extra 0 length byte
  uint8_t r255[255] = {0};
  ln = latm_wrap(r255, 255, latm_out, sizeof(latm_out));
  CHECK(ln == 257 && latm_out[0] == 0xFF && latm_out[1] == 0 && latm_frame(latm_out, ln, &back) == 255);
  printf("aac: %s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
