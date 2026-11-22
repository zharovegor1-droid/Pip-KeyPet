/* The assistant's voice codec: what the phone decodes is close to what the mic heard, packet by packet,
   and a lost packet does not spoil the ones after it. */
#include "adpcm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE 16000
#define N (RATE * 2)
#define PER 352 /* samples a packet: 180-byte notifications */

static double snr(const int16_t *a, const int16_t *b, int from, int n)
{
    double s = 0, e = 0;
    for (int i = from; i < from + n; i++) s += (double)a[i] * a[i], e += (double)(a[i] - b[i]) * (a[i] - b[i]);
    return 10 * log10(s / (e + 1));
}

int main(void)
{
    static int16_t pcm[N], back[N];
    srand(7);
    double ph = 0;
    for (int i = 0; i < N; i++) { /* a voice-ish signal: a gliding tone with formants, swelling, and a little noise */
        double t = (double)i / RATE, f0 = 140 + 40 * sin(2 * M_PI * 1.3 * t), env = 0.2 + 0.8 * fabs(sin(2 * M_PI * 2.1 * t));
        ph += 2 * M_PI * f0 / RATE;
        double x = 0;
        for (int h = 1; h <= 12; h++) x += sin(ph * h) / h * (h == 4 || h == 9 ? 2.5 : 1);
        pcm[i] = (int16_t)fmax(-32000, fmin(32000, env * x * 3000 + (rand() % 400 - 200)));
    }
    adpcm_t st = {0};
    uint8_t pkt[ADPCM_HEADER + PER / 2];
    int fails = 0, seq = 0;
    for (int at = 0; at + PER <= N; at += PER, seq++) {
        int len = adpcm_packet(&st, (uint8_t)seq, pcm + at, PER, pkt);
        if (len != ADPCM_HEADER + PER / 2 || pkt[0] != (uint8_t)seq) fails++, fprintf(stderr, "bad packet %d\n", seq);
        if (seq % 7 == 3) continue; /* lost on the air */
        if (adpcm_unpacket(pkt, len, back + at) != PER) fails++;
    }
    /* every packet that arrived decodes well on its own, including the ones right after a lost one */
    double worst = 99;
    for (int k = 0, at = 0; at + PER <= N; at += PER, k++) {
        if (k % 7 == 3) continue;
        double v = snr(pcm, back, at, PER);
        if (v < worst) worst = v;
        if (getenv("V")) printf("%d %.1f\n", k, v);
    }
    printf("adpcm: %d packets of %d bytes (%.1f KB/s), worst packet SNR %.1f dB\n", seq, ADPCM_HEADER + PER / 2,
           (ADPCM_HEADER + PER / 2) * (double)RATE / PER / 1024, worst);
    if (worst < 18) fails++, fprintf(stderr, "too noisy\n");
    /* a fixed vector for the phone's decoder (ios/PipBridge/ADPCM.swift): the first packet of a known ramp */
    adpcm_t z = {0};
    int16_t ramp[16];
    for (int i = 0; i < 16; i++) ramp[i] = (int16_t)(i * 1000 - 3000);
    int len = adpcm_packet(&z, 5, ramp, 16, pkt);
    int16_t out[16];
    adpcm_unpacket(pkt, len, out);
    printf("vector bytes:");
    for (int i = 0; i < len; i++) printf(" %02X", pkt[i]);
    printf("\nvector pcm:");
    for (int i = 0; i < 16; i++) printf(" %d", out[i]);
    printf("\n");
    return fails ? 1 : 0;
}
