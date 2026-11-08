// blue_a2dp: command line tool for the desktop build (Linux / macOS, USB Bluetooth dongle).
//
//   blue_a2dp sink   [options]                receive audio: to speakers, a WAV file or stdout (PCM)
//   blue_a2dp source [options] DEVICE         send a tone or a WAV / MP3 / AAC file to DEVICE
//   blue_a2dp play   FILE [options]           decode a file like the source does (no Bluetooth needed)
//   blue_a2dp test   [sbc|aac|aptx|mp3|wav]   hardware free self tests (default: all)
//
// Log messages go to stderr so that the PCM data on stdout can be piped, e.g.
//   blue_a2dp sink --usb 1:2 | aplay -f S16_LE -r 44100 -c 2
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "BTstackA2DP.h"
#include "../test/tests.h"
#include "at_io.h"
#include "pcm.h"

static void usage(FILE* out) {
  fprintf(out,
          "usage: blue_a2dp <command> [options]\n"
          "\n"
          "commands:\n"
          "  sink                 receive audio from a phone / PC\n"
          "  source DEVICE        stream audio to the Bluetooth speaker named DEVICE\n"
          "  play FILE            play a .wav / .mp3 / .aac file or convert it to a WAV file; shows what the\n"
          "                       source would send (no Bluetooth needed). Output: --play, --wav FILE or stdout\n"
          "  test [NAME]          hardware free self tests: sbc, aac, aptx, mp3, wav (default: all)\n"
          "  help                 this text\n"
          "\n"
          "options for sink and source:\n"
          "  --usb BUS:PORT[.PORT...]  USB Bluetooth dongle, see `lsusb -t` (recommended: otherwise\n"
          "                            the first controller, maybe a built-in one, is used)\n"
          "  --hci N                   Linux controller hciN through the HCI user channel (e.g. the\n"
          "                            onboard Bluetooth of a Raspberry Pi), needs root and\n"
          "                            `systemctl stop bluetooth`; instead of --usb\n"
          "  --name NAME               name of this device (sink default: BTstackA2DP-desktop)\n"
          "  --bonding FILE            file for the pairing data (default: ~/.blue_a2dp_bonding.tlv)\n"
          "  --no-bonding              keep the pairing data in RAM only\n"
          "\n"
          "options for sink (outputs can be combined; without any, 16 bit PCM goes to stdout):\n"
          "  --play                    play on the speakers (Miniaudio, needs the audio-tools build)\n"
          "  --wav FILE                write a WAV file (finished when you press Ctrl-C)\n"
          "\n"
          "options for play:\n"
          "  --rate HZ                 sample rate of the output (default 44100)\n"
          "\n"
          "options for source (default: a test tone):\n"
          "  --file FILE               send a .wav file (.mp3 / .aac need the audio-tools build)\n"
          "  --loop                    repeat the file\n"
          "  --tone HZ                 frequency of the test tone (default 440)\n"
          "\n"
          "audio-tools build: %s\n",
          cli::audio_tools_available() ? "yes" : "no (cmake -DBTSTACKA2DP_AUDIO_TOOLS=ON, see docs/build.md)");
}

// ---- options ------------------------------------------------------------------------
struct Options {
  const char* usb = nullptr;
  const char* hci = nullptr;
  const char* name = nullptr;
  const char* bonding = nullptr;
  bool no_bonding = false;
  bool play = false;
  const char* wav = nullptr;
  const char* file = nullptr;
  bool loop = false;
  float tone = 440.0f;
  uint32_t rate = 44100;
  const char* arg = nullptr;  // positional: the remote device (source) / test name
};

static bool parse(int argc, char** argv, int first, Options& o) {
  for (int i = first; i < argc; i++) {
    const char* a = argv[i];
    auto value = [&](const char** out) -> bool {
      if (i + 1 >= argc) {
        fprintf(stderr, "option %s needs a value\n", a);
        return false;
      }
      *out = argv[++i];
      return true;
    };
    const char* tmp = nullptr;
    if (!strcmp(a, "--usb")) {
      if (!value(&o.usb)) return false;
    } else if (!strcmp(a, "--hci")) {
      if (!value(&o.hci)) return false;
    } else if (!strcmp(a, "--name")) {
      if (!value(&o.name)) return false;
    } else if (!strcmp(a, "--bonding")) {
      if (!value(&o.bonding)) return false;
    } else if (!strcmp(a, "--no-bonding")) {
      o.no_bonding = true;
    } else if (!strcmp(a, "--play")) {
      o.play = true;
    } else if (!strcmp(a, "--wav")) {
      if (!value(&o.wav)) return false;
    } else if (!strcmp(a, "--file")) {
      if (!value(&o.file)) return false;
    } else if (!strcmp(a, "--loop")) {
      o.loop = true;
    } else if (!strcmp(a, "--tone")) {
      if (!value(&tmp)) return false;
      o.tone = (float)atof(tmp);
      if (o.tone <= 0) {
        fprintf(stderr, "invalid tone frequency '%s'\n", tmp);
        return false;
      }
    } else if (!strcmp(a, "--rate")) {
      if (!value(&tmp)) return false;
      o.rate = (uint32_t)atoi(tmp);
      if (o.rate < 8000 || o.rate > 192000) {
        fprintf(stderr, "invalid sample rate '%s'\n", tmp);
        return false;
      }
    } else if (a[0] == '-') {
      fprintf(stderr, "unknown option %s\n", a);
      return false;
    } else if (!o.arg) {
      o.arg = a;
    } else {
      fprintf(stderr, "unexpected argument %s\n", a);
      return false;
    }
  }
  return true;
}

// applies --usb / --hci / --bonding to the port before the stack is started
static bool configure_port(const Options& o) {
  if (o.usb && o.hci) {
    fprintf(stderr, "use either --usb or --hci, not both\n");
    return false;
  }
  if (o.usb && !blue_a2dp::Port::set_usb_path(o.usb)) {
    fprintf(stderr, "bad USB path '%s', expected BUS:PORT[.PORT...]\n", o.usb);
    return false;
  }
  if (o.hci) {
    char* end = nullptr;
    long id = strtol(o.hci, &end, 10);
    if (end == o.hci || *end != '\0' || id < 0 || id > 255) {
      fprintf(stderr, "bad controller number '%s', expected e.g. --hci 0\n", o.hci);
      return false;
    }
    blue_a2dp::Port::set_hci_device((int)id);
  }
  if (o.no_bonding) blue_a2dp::Port::set_bonding_file(nullptr);
  else if (o.bonding) blue_a2dp::Port::set_bonding_file(o.bonding);
  return true;
}

// the stack is started in its own thread: wait until the controller reports that it is up
template <class T>
static bool wait_ready(T& a2dp, int seconds = 6) {
  for (int i = 0; i < seconds * 10 && !a2dp.is_ready(); i++) usleep(100000);
  if (!a2dp.is_ready())
    fprintf(stderr,
            "no Bluetooth controller found or it cannot be opened. Check --usb (see `lsusb -t`) or\n"
            "--hci (see `hciconfig -a`) and that you have access to it (udev rule or root), see docs/build.md\n"
            "If it worked before: unplug and replug the dongle (some cheap ones hang after a run).\n");
  if (!a2dp.is_ready()) {
    a2dp.end();      // power down and close the transport, so that the OS gets the dongle back
    usleep(500000);
    return false;
  }
  return true;
}

// Ctrl-C ends the loops below, so that files are finished properly
static volatile sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }
static void install_signal_handlers() {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
}

static void on_conn(blue_a2dp::ConnectionState s, void*) {
  static const char* names[] = {"disconnected", "connecting", "connected", "disconnecting"};
  fprintf(stderr, "connection: %s\n", names[(int)s]);
}

// ---- sink ----------------------------------------------------------------------------
/// Sends the received audio to stdout, a WAV file and / or the speakers
class SinkOutput {
 public:
  bool to_stdout = false;
  const char* wav_path = nullptr;
  bool play = false;

  /// Called when the format of the stream is known (or changes)
  void format(uint32_t rate, int channels) {
    std::lock_guard<std::mutex> lock(mu_);
    if (wav_path && !wav_.is_open()) {
      if (wav_.open(wav_path, rate, channels))
        fprintf(stderr, "writing %s (%u Hz, %d channels)\n", wav_path, (unsigned)rate, channels);
      else
        fprintf(stderr, "cannot create %s\n", wav_path);
    }
    if (play) {
      if (!player_) player_.reset(cli::create_player());
      if (player_) {
        player_->end();
        if (!player_->begin(rate, channels)) fprintf(stderr, "cannot open the audio output\n");
      }
    }
  }

  void write(const uint8_t* data, uint32_t len) {
    std::lock_guard<std::mutex> lock(mu_);
    if (to_stdout) {
      fwrite(data, 1, len, stdout);
      fflush(stdout);
    }
    wav_.write(data, len);
    if (player_) player_->write(data, len);
  }

  void close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (wav_.is_open()) {
      uint64_t bytes = wav_.bytes();
      wav_.close();
      fprintf(stderr, "%s: %llu bytes of audio\n", wav_path, (unsigned long long)bytes);
    }
    if (player_) player_->end();
  }

 private:
  std::mutex mu_;
  cli::WavWriter wav_;
  std::unique_ptr<cli::PcmPlayer> player_;
};

static SinkOutput g_out;

static void on_audio(const uint8_t* data, uint32_t len) { g_out.write(data, len); }
static void on_meta(uint8_t id, const uint8_t* text) { fprintf(stderr, "metadata 0x%02x: %s\n", id, text); }
static void on_info(const blue_a2dp::AudioInfo& i, void*) {
  fprintf(stderr, "audio: %u Hz, %u channels\n", (unsigned)i.sample_rate, i.channels);
  g_out.format(i.sample_rate, i.channels);
}

static int run_sink(const Options& o) {
  g_out.wav_path = o.wav;
  g_out.play = o.play;
  g_out.to_stdout = !o.wav && !o.play;
  if (o.play && !cli::audio_tools_available()) {
    fprintf(stderr, "--play needs the audio-tools build: cmake -DBTSTACKA2DP_AUDIO_TOOLS=ON (see docs/build.md)\n");
    return 2;
  }
  if (g_out.to_stdout && isatty(STDOUT_FILENO)) {
    fprintf(stderr,
            "no output selected and stdout is a terminal: use --play, --wav FILE or pipe the 16 bit PCM,\n"
            "e.g. | aplay -f S16_LE -r 44100 -c 2\n");
    return 2;
  }
  if (!configure_port(o)) return 2;
  install_signal_handlers();

  BluetoothA2DPSink sink;
  sink.set_stream_reader(on_audio);
  sink.set_avrc_metadata_callback(on_meta);
  sink.set_on_connection_state_changed(on_conn);
  sink.set_audio_info_callback(on_info);
  if (!sink.start(o.name ? o.name : "BTstackA2DP-desktop")) {
    fprintf(stderr, "start failed (no Bluetooth dongle accessible? see --usb and docs/build.md)\n");
    return 1;
  }
  if (!wait_ready(sink)) return 1;
  fprintf(stderr, "sink running - press Ctrl-C to stop\n");
  while (!g_stop) usleep(100000);
  sink.end();
  usleep(200000);  // let the Bluetooth thread finish its last callbacks
  g_out.close();
  return 0;
}

// ---- source --------------------------------------------------------------------------
static BluetoothA2DPSource* g_source = nullptr;
static std::unique_ptr<cli::PcmReader> g_reader;
static std::unique_ptr<cli::StereoFeed> g_feed;
static volatile bool g_eof = false;
static float g_phase = 0;
static float g_tone = 440.0f;

static uint32_t stream_rate() {
  uint32_t r = g_source ? g_source->get_sample_rate() : 0;
  return r ? r : 44100;  // 44.1 kHz is what we ask for
}

static int32_t sine(Frame* f, int32_t n) {
  float step = 2 * (float)M_PI * g_tone / (float)stream_rate();
  for (int i = 0; i < n; i++) {
    f[i] = Frame((int)(sinf(g_phase) * 8000));
    g_phase += step;
    if (g_phase > 2 * (float)M_PI) g_phase -= 2 * (float)M_PI;
  }
  return n;
}

static int32_t from_file(Frame* f, int32_t n) {
  int32_t got = (int32_t)g_feed->read((int16_t*)f, (size_t)n, stream_rate());
  if (got < n) g_eof = true;
  return got;
}

static bool has_extension(const char* path, const char* ext) {
  size_t a = strlen(path), b = strlen(ext);
  return a > b && !strcasecmp(path + a - b, ext);
}

/// Opens a .wav file natively and .mp3 / .aac through the audio-tools decoders
static bool open_input(const char* path, std::string& err) {
  if (has_extension(path, ".wav")) {
    auto* wav = new cli::WavReader();
    g_reader.reset(wav);
    return wav->open(path, err);
  }
  if (has_extension(path, ".mp3") || has_extension(path, ".aac")) {
    g_reader.reset(cli::open_decoder(path, err));
    return g_reader != nullptr;
  }
  err = "unsupported file type (.wav, .mp3 or .aac expected): " + std::string(path);
  return false;
}

static int run_source(const Options& o) {
  if (!o.arg) {
    fprintf(stderr, "source needs the name of the Bluetooth device, e.g. blue_a2dp source \"My Speaker\"\n");
    return 2;
  }
  blue_a2dp::FrameDataCallback cb = sine;
  if (o.file) {
    std::string err;
    if (!open_input(o.file, err)) {
      fprintf(stderr, "%s\n", err.c_str());
      return 2;
    }
    g_feed.reset(new cli::StereoFeed(*g_reader, o.loop));
    fprintf(stderr, "%s: %u Hz, %d channels\n", o.file, (unsigned)g_reader->rate(), g_reader->channels());
    cb = from_file;
  }
  if (!configure_port(o)) return 2;
  g_tone = o.tone;
  install_signal_handlers();

  BluetoothA2DPSource source;
  g_source = &source;
  if (o.name) source.set_local_name(o.name);
  source.set_on_connection_state_changed(on_conn);
  if (!source.start(o.arg, cb)) {
    fprintf(stderr, "start failed (no Bluetooth dongle accessible? see --usb and docs/build.md)\n");
    return 1;
  }
  if (!wait_ready(source)) return 1;
  fprintf(stderr, "searching for '%s' (put it into pairing mode) - press Ctrl-C to stop\n", o.arg);
  while (!g_stop && !g_eof) usleep(100000);
  if (g_eof) {
    fprintf(stderr, "end of file\n");
    sleep(2);  // the last audio is still on its way to the speaker
  }
  source.end();
  usleep(200000);
  return 0;
}

// ---- play ----------------------------------------------------------------------------
static int run_play(const Options& o) {
  if (!o.arg) {
    fprintf(stderr, "play needs a file, e.g. blue_a2dp play song.mp3 --wav out.wav\n");
    return 2;
  }
  g_out.wav_path = o.wav;
  g_out.play = o.play;
  g_out.to_stdout = !o.wav && !o.play;
  if (o.play && !cli::audio_tools_available()) {
    fprintf(stderr, "--play needs the audio-tools build: cmake -DBTSTACKA2DP_AUDIO_TOOLS=ON (see docs/build.md)\n");
    return 2;
  }
  if (g_out.to_stdout && isatty(STDOUT_FILENO)) {
    fprintf(stderr, "no output selected and stdout is a terminal: use --play or --wav FILE\n");
    return 2;
  }
  if (o.loop && !o.play) {
    fprintf(stderr, "--loop only makes sense with --play (a file or pipe would grow without limit)\n");
    return 2;
  }
  std::string err;
  if (!open_input(o.arg, err)) {
    fprintf(stderr, "%s\n", err.c_str());
    return 2;
  }
  fprintf(stderr, "%s: %u Hz, %d channels -> %u Hz stereo\n", o.arg, (unsigned)g_reader->rate(),
          g_reader->channels(), (unsigned)o.rate);
  install_signal_handlers();
  cli::StereoFeed feed(*g_reader, o.loop);
  g_out.format(o.rate, 2);
  std::vector<int16_t> buf(2 * 1024);
  size_t frames = 0, got;
  while (!g_stop && (got = feed.read(buf.data(), 1024, o.rate)) > 0) {
    g_out.write((const uint8_t*)buf.data(), (uint32_t)(got * 4));
    frames += got;
  }
  g_out.close();
  fprintf(stderr, "%.2f seconds of audio\n", (double)frames / o.rate);
  return 0;
}

// ---- test ----------------------------------------------------------------------------
static int run_tests(const Options& o) {
  struct T { const char* name; int (*fn)(); };
  static const T tests[] = {{"sbc", test_sbc}, {"aac", test_aac}, {"aptx", test_aptx},
                            {"mp3", test_mp3}, {"wav", test_wav}};
  bool all = !o.arg || !strcmp(o.arg, "all");
  int failed = 0, ran = 0;
  for (const T& t : tests) {
    if (!all && strcmp(o.arg, t.name)) continue;
    ran++;
    failed += t.fn() != 0;
  }
  if (!ran) {
    fprintf(stderr, "unknown test '%s' (sbc, aac, aptx, mp3, wav, all)\n", o.arg);
    return 2;
  }
  printf("%d of %d tests failed\n", failed, ran);
  return failed ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
    usage(argc < 2 ? stderr : stdout);
    return argc < 2 ? 2 : 0;
  }
  Options o;
  if (!parse(argc, argv, 2, o)) {
    usage(stderr);
    return 2;
  }
  if (!strcmp(argv[1], "sink")) return run_sink(o);
  if (!strcmp(argv[1], "source")) return run_source(o);
  if (!strcmp(argv[1], "play")) return run_play(o);
  if (!strcmp(argv[1], "test")) return run_tests(o);
  fprintf(stderr, "unknown command '%s'\n\n", argv[1]);
  usage(stderr);
  return 2;
}
