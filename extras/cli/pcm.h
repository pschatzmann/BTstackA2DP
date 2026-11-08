// PCM helpers of the command line tool: WAV reading / writing and conversion of any source
// to 16 bit stereo at a requested sample rate. No dependencies besides the C++ library.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace cli {

/// Source of interleaved 16 bit PCM data
class PcmReader {
 public:
  virtual ~PcmReader() {}
  virtual uint32_t rate() const = 0;
  virtual int channels() const = 0;
  /// Reads up to 'frames' frames, returns the number of frames read (0 at the end)
  virtual size_t read(int16_t* out, size_t frames) = 0;
  /// Jumps back to the start (for --loop), false if not possible
  virtual bool rewind() = 0;
};

inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

// ---- WAV reader (16 bit PCM) ---------------------------------------------------------------
class WavReader : public PcmReader {
 public:
  ~WavReader() override {
    if (f_) fclose(f_);
  }

  bool open(const char* path, std::string& err) {
    f_ = fopen(path, "rb");
    if (!f_) {
      err = std::string("cannot open ") + path;
      return false;
    }
    uint8_t h[12];
    if (fread(h, 1, 12, f_) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
      err = "not a WAV file";
      return false;
    }
    bool have_fmt = false;
    for (;;) {
      uint8_t c[8];
      if (fread(c, 1, 8, f_) != 8) {
        err = have_fmt ? "WAV file without data chunk" : "WAV file without fmt chunk";
        return false;
      }
      uint32_t size = le32(c + 4);
      if (!memcmp(c, "fmt ", 4)) {
        uint8_t fmt[40];
        uint32_t n = size < sizeof(fmt) ? size : (uint32_t)sizeof(fmt);
        if (n < 16 || fread(fmt, 1, n, f_) != n) {
          err = "invalid WAV fmt chunk";
          return false;
        }
        uint16_t format = le16(fmt);
        if (format == 0xFFFE && n >= 26) format = le16(fmt + 24);  // WAVE_FORMAT_EXTENSIBLE
        channels_ = le16(fmt + 2);
        rate_ = le32(fmt + 4);
        uint16_t bits = le16(fmt + 14);
        if (format != 1 || bits != 16 || channels_ < 1 || rate_ == 0) {
          err = "only 16 bit PCM WAV files are supported (format " + std::to_string(format) + ", " +
                std::to_string(bits) + " bit)";
          return false;
        }
        if (size > n) fseek(f_, (long)(size - n), SEEK_CUR);
        if (size & 1) fseek(f_, 1, SEEK_CUR);
        have_fmt = true;
      } else if (!memcmp(c, "data", 4)) {
        if (!have_fmt) {
          err = "WAV data chunk before fmt chunk";
          return false;
        }
        data_start_ = ftell(f_);
        data_size_ = size;  // 0 or 0xFFFFFFFF: streamed file, read until the end
        remaining_ = size;
        return true;
      } else {
        fseek(f_, (long)size + (long)(size & 1), SEEK_CUR);
      }
    }
  }

  uint32_t rate() const override { return rate_; }
  int channels() const override { return channels_; }

  size_t read(int16_t* out, size_t frames) override {
    size_t frame_bytes = (size_t)channels_ * 2;
    size_t want = frames * frame_bytes;
    bool bounded = data_size_ != 0 && data_size_ != 0xFFFFFFFFu;
    if (bounded && want > remaining_) want = remaining_;
    size_t got = fread(out, 1, want, f_) / frame_bytes;
    if (bounded) remaining_ -= (uint32_t)(got * frame_bytes);
    return got;
  }

  bool rewind() override {
    remaining_ = data_size_;
    return f_ && fseek(f_, data_start_, SEEK_SET) == 0;
  }

 private:
  FILE* f_ = nullptr;
  uint32_t rate_ = 0;
  int channels_ = 0;
  long data_start_ = 0;
  uint32_t data_size_ = 0;
  uint32_t remaining_ = 0;
};

// ---- WAV writer -----------------------------------------------------------------------------
class WavWriter {
 public:
  ~WavWriter() { close(); }
  bool is_open() const { return f_ != nullptr; }

  bool open(const char* path, uint32_t rate, int channels) {
    close();
    f_ = fopen(path, "wb");
    if (!f_) return false;
    data_bytes_ = 0;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, (uint16_t)channels);
    put32(h + 24, rate);
    put32(h + 28, rate * channels * 2);
    put16(h + 32, (uint16_t)(channels * 2));
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, 0);
    return fwrite(h, 1, sizeof(h), f_) == sizeof(h);
  }

  void write(const uint8_t* data, size_t len) {
    if (f_) data_bytes_ += fwrite(data, 1, len, f_);
  }

  uint64_t bytes() const { return data_bytes_; }

  /// Writes the final sizes into the header
  void close() {
    if (!f_) return;
    uint8_t b[4];
    uint32_t n = data_bytes_ > 0xFFFFFFF0ull ? 0xFFFFFFF0u : (uint32_t)data_bytes_;
    put32(b, 36 + n);
    fseek(f_, 4, SEEK_SET);
    fwrite(b, 1, 4, f_);
    put32(b, n);
    fseek(f_, 40, SEEK_SET);
    fwrite(b, 1, 4, f_);
    fclose(f_);
    f_ = nullptr;
  }

 private:
  FILE* f_ = nullptr;
  uint64_t data_bytes_ = 0;
};

// ---- conversion to stereo at a requested sample rate ---------------------------------------
/// Delivers any PcmReader as 16 bit stereo at the requested sample rate (linear interpolation).
class StereoFeed {
 public:
  StereoFeed(PcmReader& in, bool loop) : in_(in), loop_(loop) {}

  /// True once the end of the stream has been reached (and loop is off)
  bool finished() const { return finished_; }

  /// Writes up to 'frames' stereo frames; fewer only at the end of the stream
  size_t read(int16_t* out, size_t frames, uint32_t out_rate) {
    if (out_rate == 0) out_rate = in_.rate();
    double step = (double)in_.rate() / (double)out_rate;
    size_t n = 0;
    while (n < frames) {
      if (avail() < 2 && !fill()) {
        // the very last frame has no successor to interpolate with: pass it on as it is
        if (avail() == 1 && frac_ == 0.0 && n < frames) {
          out[2 * n] = buf_[2 * i_];
          out[2 * n + 1] = buf_[2 * i_ + 1];
          n++;
          i_++;
        }
        break;
      }
      const int16_t* a = &buf_[2 * i_];
      const int16_t* b = a + 2;
      out[2 * n] = (int16_t)(a[0] + (b[0] - a[0]) * frac_);
      out[2 * n + 1] = (int16_t)(a[1] + (b[1] - a[1]) * frac_);
      n++;
      frac_ += step;
      size_t adv = (size_t)frac_;  // input frames to move on (more than one if downsampling by > 2)
      frac_ -= (double)adv;
      i_ += adv;
      size_t total = buf_.size() / 2;
      if (i_ > total) {  // beyond the data we have: skip these frames when the next chunk arrives
        skip_ += i_ - total;
        i_ = total;
      }
    }
    if (n < frames) finished_ = true;
    return n;
  }

 private:
  size_t avail() const { return buf_.size() / 2 - i_; }

  /// Appends the next chunk of the input as stereo frames (honouring frames to skip)
  bool fill() {
    // drop consumed frames, keep the one still needed for interpolation
    if (i_ > 0) {
      buf_.erase(buf_.begin(), buf_.begin() + 2 * i_);
      i_ = 0;
    }
    const size_t chunk = 1024;
    int ch = in_.channels();
    tmp_.resize(chunk * ch);
    for (;;) {
      size_t got = in_.read(tmp_.data(), chunk);
      if (got == 0 && loop_ && in_.rewind()) got = in_.read(tmp_.data(), chunk);
      if (got == 0) return false;
      size_t start = skip_ < got ? skip_ : got;
      skip_ -= start;
      for (size_t f = start; f < got; f++) {
        int16_t l = tmp_[f * ch];
        int16_t r = ch > 1 ? tmp_[f * ch + 1] : l;
        buf_.push_back(l);
        buf_.push_back(r);
      }
      if (start < got) return true;
    }
  }

  PcmReader& in_;
  bool loop_;
  bool finished_ = false;
  std::vector<int16_t> buf_, tmp_;
  size_t i_ = 0;
  size_t skip_ = 0;
  double frac_ = 0.0;
};

}  // namespace cli
