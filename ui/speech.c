#include "speech.h"
#include "adpcm.h"
#include "voice.h"
#include <stdlib.h>

#define RING (16000 * 40)  /* 40 s: the phone sends faster than he speaks, a whole answer fits (1.25 MB, PSRAM) */
#define PREROLL 4000       /* a quarter of a second in hand before the first word */
#define WAIT_MAX 16000     /* nothing more for a second after the end: done */

static int16_t *ring;
static volatile uint32_t head, tail; /* written by speech_feed, read by speech_render; both only grow */
static volatile bool active, ended, playing;
static uint32_t starved;

void speech_feed(const uint8_t *packet, int len)
{
    if (!ring && !(ring = malloc(sizeof(int16_t) * RING))) return;
    if (!active) head = tail = 0, ended = playing = false, starved = 0, active = true;
    int16_t pcm[512];
    int n = adpcm_unpacket(packet, len > ADPCM_HEADER + 256 ? ADPCM_HEADER + 256 : len, pcm);
    for (int i = 0; i < n && head - tail < RING; i++) ring[head++ % RING] = pcm[i]; /* full: the rest is dropped */
}

void speech_end(void) { if (active) ended = true; }
void speech_stop(void) { active = playing = false; }
bool speech_busy(void) { return active; }

void speech_render(int16_t *out, int n)
{
    if (active && !playing && (head - tail >= PREROLL || ended)) playing = true;
    float vol = voice_volume();
    for (int i = 0; i < n; i++) {
        if (playing && tail != head) out[i] = (int16_t)(ring[tail++ % RING] * vol), starved = 0;
        else out[i] = 0, starved += playing;
    }
    if (active && ended && tail == head && starved > WAIT_MAX / 8) active = playing = false;
    else if (active && !ended && starved > WAIT_MAX * 4) active = playing = false; /* the phone went quiet mid-way */
}
