/* What is left of the pet: asleep face down, a fright's aftertaste, steps by day, the clock. Run by sim/check.py. */
#include "moods.h"
#include "pet.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)
#define H 3600LL
static const int64_t NOON = 1791633600; /* 2026-10-10 12:00 UTC */

int main(void)
{
    pet_t p;
    pet_init(&p, NOON, 0);
    CHECK(p.version == PET_VERSION && pet_mood(&p) == PIP_NEUTRAL, "calm when fresh");

    /* no needs any more: days alone change nothing */
    pet_update(&p, NOON + 400 * 24 * H, 0);
    CHECK(pet_mood(&p) == PIP_NEUTRAL && !p.asleep, "a year alone: still calm, still awake");
    CHECK(p.last == NOON + 400 * 24 * H, "the clock follows");

    /* face down: asleep until picked up */
    pet_event(&p, PET_SLEEP);
    pet_why_t w;
    CHECK(pet_mood_why(&p, &w) == PIP_ASLEEP && w.kind == PET_WHY_ASLEEP, "face down -> asleep");
    pet_update(&p, p.last + 12 * H, 0);
    CHECK(p.asleep, "no waking on his own");
    CHECK(pet_event(&p, PET_WAKE) == PIP_SURPRISED && !p.asleep, "picked up -> awake, surprised");
    CHECK(pet_event(&p, PET_WAKE) == -1, "already awake: nothing");

    /* a fright lasts a while, then wears off */
    pet_event(&p, PET_FRIGHT);
    CHECK(pet_mood_why(&p, &w) == PIP_AFRAID && w.kind == PET_WHY_AFTER, "a bang -> afraid");
    pet_update(&p, p.last + (int64_t)pet_tune.after_fright + 1, 0);
    CHECK(pet_mood(&p) == PIP_NEUTRAL, "and calm again");

    /* steps count by local day */
    for (int i = 0; i < 1234; i++) pet_event(&p, PET_STEP);
    CHECK(p.steps_today == 1234, "steps counted");
    pet_update(&p, p.last + 24 * H, 0);
    CHECK(p.steps_today == 0, "a new day starts from zero");

    CHECK(pet_knob("loud_db") && !pet_knob("meet_shy"), "only the bang and fright knobs are left");
    puts("pet_test ok");
    return 0;
}
