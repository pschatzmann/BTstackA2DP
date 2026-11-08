// Plug-in interface for additional A2DP sink codecs (SBC is built in).
// A codec advertises its capabilities, is configured from the btstack configuration
// event and decodes the media payload (after the RTP header) to 16 bit PCM.
#pragma once

#include <stdint.h>

namespace blue_a2dp {

/// Receiver of decoded audio (implemented by the sink)
class PcmOutput {
 public:
  virtual ~PcmOutput() {}
  /// interleaved 16 bit samples
  virtual void on_pcm(int16_t* data, int frames, int channels, int rate) = 0;
};

class SinkCodec {
 public:
  virtual ~SinkCodec() {}
  /// avdtp_media_codec_type_t, e.g. AVDTP_CODEC_MPEG_2_4_AAC (0x02)
  virtual uint8_t codec_type() const = 0;
  /// Capability bytes advertised in the AVDTP stream endpoint (must stay valid)
  virtual const uint8_t* capabilities(uint8_t& len) = 0;
  /// Buffer btstack uses to store the selected configuration (must stay valid)
  virtual uint8_t* config_buffer(uint8_t& len) = 0;
  /// Called with every A2DP_SUBEVENT_*; returns true if the event configured this codec
  virtual bool configure(const uint8_t* event) = 0;
  /// false if the media packets carry the codec data directly, without RTP header (aptX)
  virtual bool uses_rtp() const { return true; }
  /// Start of the stream
  virtual bool open(PcmOutput* out) = 0;
  /// Media payload without the RTP header
  virtual void decode(const uint8_t* payload, uint16_t len) = 0;
  virtual void close() = 0;

  uint8_t seid = 0;  // local stream endpoint id, set by the sink
};

/// Optional source codec: encodes PCM and selects its own configuration among the
/// capabilities of the remote device (btstack only does this automatically for SBC).
class SourceCodec {
 public:
  virtual ~SourceCodec() {}
  /// avdtp_media_codec_type_t, e.g. AVDTP_CODEC_MPEG_2_4_AAC (0x02)
  virtual uint8_t codec_type() const = 0;
  /// Capability bytes of the local stream endpoint (must stay valid)
  virtual const uint8_t* capabilities(uint8_t& len) = 0;
  /// Buffer btstack uses to store the selected configuration (must stay valid)
  virtual uint8_t* config_buffer(uint8_t& len) = 0;
  /// Called with each A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_*_CAPABILITY event of the remote
  /// device. If it fits, register the configuration with btstack
  /// (a2dp_source_set_config_*) and return true.
  virtual bool select(const uint8_t* capability_event) = 0;
  /// Called with the A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_*_CONFIGURATION event; returns
  /// true if it is the configuration of this codec.
  virtual bool configure(const uint8_t* configuration_event) = 0;
  /// false if the codec data is sent without RTP header (aptX)
  virtual bool uses_rtp() const { return true; }
  virtual uint32_t sample_rate() const = 0;
  /// Number of stereo PCM frames consumed by one call to encode()
  virtual uint16_t frames_per_packet() const = 0;
  virtual bool open() = 0;
  virtual void close() = 0;
  /// Encodes frames_per_packet() interleaved stereo frames. Writes one media payload
  /// (everything after the RTP header) to 'out' and returns its length, 0 if the
  /// encoder has no output yet.
  virtual uint16_t encode(const int16_t* pcm, uint8_t* out, uint16_t max_len) = 0;

  uint8_t seid = 0;  // local stream endpoint id, set by the source
};

}  // namespace blue_a2dp
