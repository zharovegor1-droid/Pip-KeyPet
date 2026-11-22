/* Pip reading an answer aloud: speech the iPhone synthesized, streamed in as ADPCM packets (ui/adpcm.h) and played on
   his own speaker. One writer (the Bluetooth task) and one reader (the audio task), no lock between them. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void speech_feed(const uint8_t *packet, int len); /* a packet of the voice; the first one starts it */
void speech_end(void);                            /* no more packets: play what is left, then stop */
void speech_stop(void);                           /* hush now */
bool speech_busy(void);                           /* something to play, or more coming */
/* n mono 16-bit samples at 16 kHz for the speaker; silence while it waits for more. */
void speech_render(int16_t *out, int n);
