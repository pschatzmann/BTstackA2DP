// Hardware free test of the WAV reader / writer and the stereo / sample rate conversion of
// the command line tool.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../cli/pcm.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

// writes 'seconds' of a sine wave as WAV
static void make_wav(const char* path, uint32_t rate, int ch, double seconds, double hz) {
  cli::WavWriter w;
  CHECK(w.open(path, rate, ch));
  size_t n = (size_t)(rate * seconds);
  std::vector<int16_t> pcm(n * ch);
  for (size_t i = 0; i < n; i++)
    for (int c = 0; c < ch; c++) pcm[i * ch + c] = (int16_t)(sin(2 * M_PI * hz * i / rate) * 10000);
  w.write((const uint8_t*)pcm.data(), pcm.size() * 2);
  w.close();
}

static double rms(const std::vector<int16_t>& v, size_t from) {
  double s = 0;
  for (size_t i = from; i < v.size(); i++) s += (double)v[i] * v[i];
  return sqrt(s / (v.size() - from));
}

int test_wav() {
  const char* path = "/tmp/blue_a2dp_test.wav";

  // round trip: stereo 44.1 kHz, 1 second
  make_wav(path, 44100, 2, 1.0, 440);
  {
    cli::WavReader r;
    std::string err;
    CHECK(r.open(path, err));
    CHECK(r.rate() == 44100 && r.channels() == 2);
    std::vector<int16_t> buf(2 * 5000);
    size_t total = 0, got;
    while ((got = r.read(buf.data(), 5000)) > 0) total += got;
    CHECK(total == 44100);
    CHECK(r.rewind() && r.read(buf.data(), 10) == 10);
  }

  // mono 22.05 kHz -> stereo 44.1 kHz: twice the frames, both channels equal, same level
  make_wav(path, 22050, 1, 1.0, 440);
  {
    cli::WavReader r;
    std::string err;
    CHECK(r.open(path, err) && r.channels() == 1);
    cli::StereoFeed feed(r, false);
    std::vector<int16_t> out, buf(2 * 1000);
    size_t got;
    while ((got = feed.read(buf.data(), 1000, 44100)) > 0) out.insert(out.end(), buf.begin(), buf.begin() + 2 * got);
    CHECK(out.size() / 2 > 44100 - 5 && out.size() / 2 < 44100 + 5);
    bool same = true;
    for (size_t i = 0; i < out.size(); i += 2) same = same && out[i] == out[i + 1];
    CHECK(same);
    CHECK(fabs(rms(out, 1000) - 7071) < 300);
    CHECK(feed.finished());
  }

  // downsampling by more than 2 (96 kHz -> 44.1 kHz) must not run past the buffer
  make_wav(path, 96000, 2, 0.5, 1000);
  {
    cli::WavReader r;
    std::string err;
    CHECK(r.open(path, err));
    cli::StereoFeed feed(r, false);
    std::vector<int16_t> out, buf(2 * 777);
    size_t got;
    while ((got = feed.read(buf.data(), 777, 44100)) > 0) out.insert(out.end(), buf.begin(), buf.begin() + 2 * got);
    CHECK(out.size() / 2 > 22050 - 20 && out.size() / 2 < 22050 + 20);
    CHECK(fabs(rms(out, 1000) - 7071) < 300);
  }

  // looping: asking for more than the file holds keeps delivering
  make_wav(path, 44100, 2, 0.1, 440);
  {
    cli::WavReader r;
    std::string err;
    CHECK(r.open(path, err));
    cli::StereoFeed feed(r, true);
    std::vector<int16_t> buf(2 * 20000);
    CHECK(feed.read(buf.data(), 20000, 44100) == 20000 && !feed.finished());
  }

  // errors
  {
    cli::WavReader r;
    std::string err;
    CHECK(!r.open("/nonexistent/file.wav", err));
    FILE* f = fopen(path, "wb");
    fputs("this is not a wav file at all", f);
    fclose(f);
    CHECK(!r.open(path, err) && err == "not a WAV file");
  }
  remove(path);
  printf("wav: %s\n", fails ? "FAIL" : "PASS");
  return fails != 0;
}
