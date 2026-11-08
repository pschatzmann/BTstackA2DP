// Hardware free self test of the vendored SBC codec: encodes a sine wave with the
// btstack encoder, decodes it again and checks that the tone survived.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BTstackA2DP.h"  // pulls in btstack_config.h first
#include <vector>
#include <btstack/classic/btstack_sbc_bluedroid.h>

static std::vector<int16_t>* decoded;
static void on_pcm(int16_t* data, int frames, int channels, int rate, void*) {
  for (int i = 0; i < frames * channels; i++) decoded->push_back(data[i]);
}

int test_sbc() {
  std::vector<int16_t> out;
  decoded = &out;

  btstack_sbc_encoder_bluedroid_t enc_ctx;
  const btstack_sbc_encoder_t* enc = btstack_sbc_encoder_bluedroid_init_instance(&enc_ctx);
  enc->configure(&enc_ctx, SBC_MODE_STANDARD, 16, 8, SBC_ALLOCATION_METHOD_LOUDNESS, 44100, 53,
                 SBC_CHANNEL_MODE_JOINT_STEREO);
  btstack_sbc_decoder_bluedroid_t dec_ctx;
  const btstack_sbc_decoder_t* dec = btstack_sbc_decoder_bluedroid_init_instance(&dec_ctx);
  dec->configure(&dec_ctx, SBC_MODE_STANDARD, on_pcm, nullptr);

  const int frames = enc->num_audio_frames(&enc_ctx);
  std::vector<int16_t> pcm(frames * 2);
  std::vector<uint8_t> sbc(1024);
  // the frame length is only known after the first frame has been encoded
  enc->encode_signed_16(&enc_ctx, pcm.data(), sbc.data());
  const int sbc_len = enc->sbc_buffer_length(&enc_ctx);
  double phase = 0;
  const int blocks = 200;
  for (int b = 0; b < blocks; b++) {
    for (int i = 0; i < frames; i++) {
      int16_t v = (int16_t)(sin(phase) * 10000);
      pcm[2 * i] = pcm[2 * i + 1] = v;
      phase += 2 * M_PI * 1000.0 / 44100.0;
    }
    enc->encode_signed_16(&enc_ctx, pcm.data(), sbc.data());
    dec->decode_signed_16(&dec_ctx, 0, sbc.data(), sbc_len);
  }

  // RMS of the second half (decoder settled): expected 10000/sqrt(2) = 7071
  size_t n = out.size();
  double sum = 0;
  size_t from = n / 2;
  for (size_t i = from; i < n; i++) sum += (double)out[i] * out[i];
  double rms = sqrt(sum / (n - from));
  printf("encoded %d frames of %d bytes (%d pcm frames each), decoded %zu samples, rms %.0f\n",
         blocks, sbc_len, frames, n, rms);
  bool ok = n >= (size_t)blocks * frames * 2 / 2 && fabs(rms - 7071) < 500;
  printf("sbc: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
