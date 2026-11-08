// Hardware free test of the MP3 (MPEG audio) helpers.
#include <stdio.h>
#include "BlueA2DP/codecs/Mp3Util.h"
using namespace blue_a2dp::mp3;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int test_mp3() {
  CHECK(bit_rate_kbps(1) == 32 && bit_rate_kbps(9) == 128 && bit_rate_kbps(14) == 320);
  CHECK(bit_rate_kbps(0) == 0 && bit_rate_kbps(15) == 0 && bit_rate_kbps(-1) == 0);
  CHECK(choose_bit_rate_index(0x3FFF) == 9);                 // everything -> 128 kbit/s
  CHECK(choose_bit_rate_index(1u << 8) == 8);                // only 112
  CHECK(choose_bit_rate_index((1u << 12) | (1u << 10)) == 10);  // 160 before 192
  CHECK(choose_bit_rate_index(0x0003) == -1);                // only very low rates
  CHECK(choose_sampling_frequency(0x3F) == 44100 && choose_sampling_frequency(0x01) == 48000);
  CHECK(choose_sampling_frequency(0x04) == 0);               // 32 kHz only
  printf("mp3: %s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
