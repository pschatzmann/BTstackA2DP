// Optional audio-tools features of the command line tool: playback with Miniaudio and MP3 / AAC
// decoding. They are only available if the tool is built with -DBTSTACKA2DP_AUDIO_TOOLS=ON
// (BTSTACKA2DP_CLI_AUDIO_TOOLS is defined then, and at_io.cpp is compiled).
#pragma once

#include <stdint.h>

#include <string>

#include "pcm.h"

namespace cli {

/// Audio output (speakers)
class PcmPlayer {
 public:
  virtual ~PcmPlayer() {}
  virtual bool begin(uint32_t rate, int channels) = 0;
  virtual void write(const uint8_t* data, size_t len) = 0;  // 16 bit interleaved
  virtual void end() = 0;
};

#ifdef BTSTACKA2DP_CLI_AUDIO_TOOLS
/// Playback with the audio-tools MiniAudioStream
PcmPlayer* create_player();
/// Decodes an .mp3 or .aac (ADTS) file with the audio-tools decoders (libhelix)
PcmReader* open_decoder(const char* path, std::string& err);
inline bool audio_tools_available() { return true; }
#else
inline PcmPlayer* create_player() { return nullptr; }
inline PcmReader* open_decoder(const char*, std::string& err) {
  err = "MP3 / AAC files need the audio-tools build: cmake -DBTSTACKA2DP_AUDIO_TOOLS=ON (see docs/build.md)";
  return nullptr;
}
inline bool audio_tools_available() { return false; }
#endif

}  // namespace cli
