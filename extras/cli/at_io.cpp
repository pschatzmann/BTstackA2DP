// Optional audio-tools features of the command line tool (see at_io.h): playback with the
// MiniAudioStream and MP3 / AAC decoding with libhelix. Compiled with -DBLUEA2DP_AUDIO_TOOLS=ON.
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioLibs/MiniAudioStream.h"

#include <stdio.h>
#include <unistd.h>

#include <vector>

#include "at_io.h"

namespace cli {

// ---- playback ---------------------------------------------------------------------------
class MiniAudioPlayer : public PcmPlayer {
 public:
  bool begin(uint32_t rate, int channels) override {
    auto cfg = out_.defaultConfig(audio_tools::TX_MODE);
    cfg.sample_rate = rate;
    cfg.channels = channels;
    cfg.bits_per_sample = 16;
    return out_.begin(cfg);
  }

  void write(const uint8_t* data, size_t len) override {
    // the audio device takes the data at its own pace: wait until everything is accepted
    while (len > 0) {
      size_t n = out_.write(data, len);
      data += n;
      len -= n;
      if (n == 0) usleep(2000);
    }
  }

  void end() override { out_.end(); }

 private:
  audio_tools::MiniAudioStream out_;
};

PcmPlayer* create_player() { return new MiniAudioPlayer(); }

// ---- MP3 / AAC files -------------------------------------------------------------------
/// Feeds a file through an audio-tools decoder; the decoded PCM arrives via Print::write
template <class Decoder>
class DecoderReader : public PcmReader, public Print {
 public:
  ~DecoderReader() override {
    dec_.end();
    if (f_) fclose(f_);
  }

  bool open(const char* path, std::string& err) {
    f_ = fopen(path, "rb");
    if (!f_) {
      err = std::string("cannot open ") + path;
      return false;
    }
    dec_.setOutput(*this);
    dec_.begin();
    // decode until the format is known
    while (rate_ == 0 && feed()) {
    }
    if (rate_ == 0) {
      err = std::string("no audio found in ") + path;
      return false;
    }
    return true;
  }

  uint32_t rate() const override { return rate_; }
  int channels() const override { return channels_; }

  size_t read(int16_t* out, size_t frames) override {
    size_t need = frames * channels_;
    while (fifo_.size() - head_ < need && feed()) {
    }
    size_t n = fifo_.size() - head_;
    if (n > need) n = need;
    n -= n % channels_;
    memcpy(out, fifo_.data() + head_, n * sizeof(int16_t));
    head_ += n;
    if (head_ > 65536) {  // drop what has been consumed
      fifo_.erase(fifo_.begin(), fifo_.begin() + head_);
      head_ = 0;
    }
    return n / channels_;
  }

  bool rewind() override {
    if (!f_ || fseek(f_, 0, SEEK_SET) != 0) return false;
    dec_.end();
    fifo_.clear();
    head_ = 0;
    dec_.begin();
    return true;
  }

  // Print: PCM from the decoder
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    audio_tools::AudioInfo info = dec_.audioInfo();
    if (info.sample_rate > 0 && info.channels > 0) {
      rate_ = info.sample_rate;
      channels_ = info.channels;
    }
    const int16_t* s = (const int16_t*)data;
    fifo_.insert(fifo_.end(), s, s + len / 2);
    return len;
  }

 private:
  /// Reads the next chunk of the file into the decoder, false at the end of the file
  bool feed() {
    uint8_t buf[2048];
    size_t n = fread(buf, 1, sizeof(buf), f_);
    if (n == 0) return false;
    dec_.write(buf, n);
    return true;
  }

  Decoder dec_;
  FILE* f_ = nullptr;
  uint32_t rate_ = 0;
  int channels_ = 2;
  std::vector<int16_t> fifo_;
  size_t head_ = 0;
};

template <class Decoder>
static PcmReader* open_with(const char* path, std::string& err) {
  DecoderReader<Decoder>* r = new DecoderReader<Decoder>();
  if (!r->open(path, err)) {
    delete r;
    return nullptr;
  }
  return r;
}

static bool ends_with(const char* path, const char* ext) {
  size_t a = strlen(path), b = strlen(ext);
  return a > b && !strcasecmp(path + a - b, ext);
}

PcmReader* open_decoder(const char* path, std::string& err) {
  if (ends_with(path, ".mp3")) return open_with<audio_tools::MP3DecoderHelix>(path, err);
  if (ends_with(path, ".aac")) return open_with<audio_tools::AACDecoderHelix>(path, err);
  err = std::string("unsupported file type: ") + path;
  return nullptr;
}

}  // namespace cli
