/* IMA ADPCM, 4 bits a sample: the assistant's voice goes to the iPhone at 16 kHz in 8 KB/s instead of 32.
   Every packet starts with the coder's state, so a lost one costs its own 20 ms and nothing after it.
   Packet: [seq u8][predictor i16 LE][step index u8][nibbles: the first sample in the low half of a byte].
   The decoder on the phone: ios/PipBridge/ADPCM.swift (keep the two in step). */
#pragma once
#include <stdint.h>

typedef struct { int16_t pred; uint8_t index; } adpcm_t;

#define ADPCM_HEADER 4
/* Encode n samples (n even) after the header; returns the packet's length. */
int adpcm_packet(adpcm_t *st, uint8_t seq, const int16_t *pcm, int n, uint8_t *out);
/* The other way, for the tests: n samples out of a packet of len bytes; returns n. */
int adpcm_unpacket(const uint8_t *in, int len, int16_t *pcm);
