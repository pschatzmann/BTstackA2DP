# Codecs
SBC is built in (btstack's own codec). All other codecs are **opt-in plug-ins** that wrap the
encoders and decoders of [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools),
so the core library does not depend on audio-tools or on any codec library.

| Codec | Sink (decoder) | Source (encoder) | Needs (besides audio-tools) |
|---|---|---|---|
| SBC (btstack) | built in | built in | - |
| SBC (audio-tools) | `A2DPCodecSBCDecoder` | `A2DPCodecSBCEncoder` | codec-sbc |
| AAC | `A2DPCodecAACDecoder` | `A2DPCodecAACEncoder` | codec-helix / codec-fdk-aac |
| aptX | `A2DPCodecAptxDecoder` | `A2DPCodecAptxEncoder` | codec-openaptx |
| MP3 (experimental) | `A2DPCodecMP3Decoder` | `A2DPCodecMP3Encoder` | codec-helix / codec-shine |

All adapters live in `BlueA2DP/codecs/` (`#include "BlueA2DP/codecs/<name>.h"`) and are
templates over the audio-tools class, e.g. `blue_a2dp::A2DPCodecAACDecoder<AACDecoderHelix>`.

## How a codec is chosen
* **Sink:** every codec added with `add_codec()` is advertised next to SBC. The remote device
  (phone, PC) picks the one it prefers; without a match SBC is used.
* **Source:** the speaker's endpoints are checked in the order it lists them and a codec it
  supports takes precedence over SBC (if several match, the one listed last wins); without a
  match SBC is used.
* **SBC from audio-tools:** `set_sbc_codec()` does not add an endpoint, it *replaces* btstack's
  SBC decoder / encoder.

## Usage
```cpp
// Sink: AAC with libhelix
AACDecoderHelix helix;
blue_a2dp::A2DPCodecAACDecoder<AACDecoderHelix> aac(helix);
sink.add_codec(&aac);                 // before sink.start()
```
```cpp
// Sink: aptX
APTXDecoder aptx_dec;
blue_a2dp::A2DPCodecAptxDecoder<APTXDecoder> aptx(aptx_dec);
sink.add_codec(&aptx);
```
```cpp
// Source: AAC with FDK (bit rate in bit/s, keep it below ~200000)
AACEncoderFDK fdk;
blue_a2dp::A2DPCodecAACEncoder<AACEncoderFDK> aac(fdk, 192000);
source.add_codec(&aac);               // before source.start()
```
```cpp
// Source: aptX
APTXEncoder aptx_enc;
blue_a2dp::A2DPCodecAptxEncoder<APTXEncoder> aptx(aptx_enc);
source.add_codec(&aptx);
```
```cpp
// Sink: MP3 (experimental, few phones offer it)
MP3DecoderHelix mp3_dec;
blue_a2dp::A2DPCodecMP3Decoder<MP3DecoderHelix> mp3(mp3_dec);
sink.add_codec(&mp3);
```
```cpp
// Source: MP3 (Shine produces one frame per packet)
MP3EncoderShine shine;
blue_a2dp::A2DPCodecMP3Encoder<MP3EncoderShine> mp3(shine);
source.add_codec(&mp3);
```
```cpp
// SBC from audio-tools instead of btstack's SBC (sink and source)
SBCDecoder sbc_dec;  blue_a2dp::A2DPCodecSBCDecoder<SBCDecoder> sbc_sink(sbc_dec);
sink.set_sbc_codec(&sbc_sink);
SBCEncoder sbc_enc;  blue_a2dp::A2DPCodecSBCEncoder<SBCEncoder> sbc_src(sbc_enc);
source.set_sbc_codec(&sbc_src);
```

## Examples
| Example | Shows |
|---|---|
| `sink-aac-i2s`, `source-aac` | AAC |
| `sink-aptx-i2s`, `source-aptx` | aptX |
| `sink-mp3-i2s`, `source-mp3` | MP3 |
| `sink-sbc-audiotools`, `source-sbc-audiotools` | SBC from audio-tools |

## Status and limits
* **Tested:** (`blue_a2dp test` runs the hardware free tests) btstack's built-in SBC sink on a real ESP32 (receiving from a PC). The LATM/ADTS
  conversion for AAC (`extras/test/aac_adts.cpp`) and the aptX codec information
  (`extras/test/aptx_info.cpp`) and MP3 helpers (`extras/test/mp3_util.cpp`) have unit tests. Everything else in the table compiles for ESP32
  (and mostly Pico W) but has **not been tried against real devices yet**; aptX in particular
  (packet format, no RTP header) still has to be confirmed.
* **Memory/CPU:** AAC needs lots of RAM (libhelix ~35 KB, FDK ~100 KB), a classic ESP32 can do it.
  On the Pico W the core's btstack allows only one stream endpoint, so extra codecs are skipped
  there and SBC is used.
* **Packets:** one encoded frame per packet, no fragmentation; hence the bit rate limit for AAC.
  The audio-tools SBC encoder does plain stereo only (no joint stereo).
* **MP3:** btstack's MPEG audio support is the least exercised part of the stack (its capability
  and configuration encoding is not symmetric) and hardly any device offers MP3 over A2DP.
  Treat it as experimental.
* **Licences:** AAC and aptX are patent-encumbered; `codec-openaptx` is only meant for evaluation
  and personal projects. Check the terms before shipping a product.
* **Details:** A2DP carries AAC as LATM while decoders expect ADTS (`codecs/AacUtil.h` converts);
  aptX is a vendor codec (id `0x4F`/`0x0001`) sent without RTP header (`uses_rtp()`).

## Writing your own codec
Implement `SinkCodec` and/or `SourceCodec` from `BlueA2DP/A2DPCodec.h`: advertise capabilities,
react to the configuration event, and convert between media payloads and 16 bit PCM. The AAC and
aptX adapters are about 100 lines each and make good templates. LC3 exists in audio-tools but is
not wired up.
