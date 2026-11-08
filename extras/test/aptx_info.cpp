// Hardware free test of the aptX codec information helpers.
#include <stdio.h>
#include "BTstackA2DP/codecs/AptxUtil.h"
using namespace blue_a2dp::aptx;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int test_aptx() {
  uint8_t info[7];
  make_info(info, kFreq44100 | kStereo);
  CHECK(is_aptx(info, 7) && !is_aptx(info, 6));
  CHECK(info[0] == 0x4F && info[4] == 0x01 && info[6] == 0x22);
  uint8_t ldac[7] = {0x2D, 0x01, 0x00, 0x00, 0xAA, 0x00, 0x00};  // Sony LDAC
  CHECK(!is_aptx(ldac, 7));
  CHECK(sample_rate(0x22) == 44100 && sample_rate(0x12) == 48000 && sample_rate(0x82) == 16000 && sample_rate(0x02) == 0);
  CHECK(channels(0x21) == 1 && channels(0x22) == 2);
  CHECK(choose(0xF2) == 0x22);                // everything -> 44.1 stereo
  CHECK(choose(0x12) == 0x12);                // only 48 kHz stereo
  CHECK(choose(0x21) == 0);                   // mono only
  CHECK(choose(0x82) == 0);                   // 16 kHz only
  printf("aptx: %s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
