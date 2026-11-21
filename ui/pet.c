/* What is left of Pip's inner state: see pet.h. */
#include "pet.h"
#include "moods.h"
#include <string.h>

static int64_t local(int64_t now, int tz_min) { return now + (int64_t)tz_min * 60; }
int pet_hour(int64_t now, int tz_min) { return (int)((local(now, tz_min) / 3600) % 24); }
static int32_t pet_day(int64_t now, int tz_min) { return (int32_t)(local(now, tz_min) / 86400); }

pet_tune_t pet_tune = {.loud_db = 75, .loud_jump = 20, .shock_db = 88, .after_fright = 30};

#define K(n, lo, hi) {#n, &pet_tune.n, lo, hi}
const pet_knob_t pet_knobs[] = {K(loud_db, 50, 110), K(loud_jump, 5, 50), K(shock_db, 60, 120), K(after_fright, 0, 300)};
#undef K
const int pet_knob_count = sizeof pet_knobs / sizeof pet_knobs[0];

float *pet_knob(const char *name)
{
    for (int i = 0; i < pet_knob_count; i++)
        if (!strcmp(pet_knobs[i].name, name)) return pet_knobs[i].v;
    return NULL;
}

void pet_init(pet_t *p, int64_t now, int tz_min)
{
    memset(p, 0, sizeof *p);
    p->version = PET_VERSION;
    p->last = now;
    p->day = pet_day(now, tz_min);
    p->bat = p->after_mood = -1;
}

void pet_update(pet_t *p, int64_t now, int tz_min)
{
    if (now > p->last) p->last = now;
    int32_t day = pet_day(p->last, tz_min);
    if (day != p->day) p->day = day, p->steps_today = 0;
    if (p->after_mood >= 0 && p->last >= p->after_until) p->after_mood = -1;
}

int pet_event(pet_t *p, int ev)
{
    switch (ev) {
    case PET_STEP: p->steps_today++; return -1;
    case PET_WAKE:
        if (!p->asleep) return -1;
        p->asleep = false;
        return PIP_SURPRISED;
    case PET_SLEEP: p->asleep = true, p->after_mood = -1; return -1;
    case PET_FRIGHT:
        if (pet_tune.after_fright > 0) p->after_mood = PIP_AFRAID, p->after_until = p->last + (int64_t)pet_tune.after_fright;
        return -1;
    }
    return -1;
}

int pet_mood_why(const pet_t *p, pet_why_t *why)
{
    pet_why_t w = {PET_WHY_CALM, 0};
    int mood = PIP_NEUTRAL;
    if (p->asleep) w.kind = PET_WHY_ASLEEP, mood = PIP_ASLEEP;
    else if (p->after_mood >= 0 && p->last < p->after_until)
        w.kind = PET_WHY_AFTER, w.value = (float)(p->after_until - p->last), mood = p->after_mood;
    if (why) *why = w;
    return mood;
}

int pet_mood(const pet_t *p) { return pet_mood_why(p, NULL); }
