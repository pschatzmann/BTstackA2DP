// Hardware free self tests, run with `blue_a2dp test`. Each returns 0 on success.
#pragma once
int test_sbc();   // SBC encode -> decode loopback with the vendored btstack codec
int test_aac();   // LATM / ADTS conversion for AAC
int test_aptx();  // aptX codec information helpers
int test_mp3();   // MP3 bit rate / sampling rate selection
int test_wav();   // WAV reader / writer and sample rate conversion of the command line tool
