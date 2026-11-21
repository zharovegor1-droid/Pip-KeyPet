/* What is left of Pip's inner state with the tamagotchi cut out: no needs, no day mood, no sulking. The face is calm
   and moves only on events (touch, shake, a drop, a bang, the assistant, the phone). This keeps the clock that survives
   reboots, being put to bed face down, today's steps and a fright's aftertaste. Pure C, no LVGL; one POD to persist. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PET_VERSION 4

typedef struct {
    uint32_t version;
    int64_t last;     /* unix time this state is valid for (main.c keeps the clock on it across reboots) */
    int32_t day;      /* local day number steps_today belongs to */
    int32_t steps_today;
    bool asleep;      /* put to bed face down, until picked up */
    bool charging;    /* kept up to date by pip.c */
    int8_t bat;       /* %, -1 unknown */
    int8_t after_mood; /* an aftertaste of a fright, until after_until (-1: none) */
    int64_t after_until;
} pet_t;
_Static_assert(sizeof(pet_t) <= 256, "the firmware reads it back into a 256-byte buffer (main.c pet_load)");

enum { PET_STEP, PET_WAKE, PET_SLEEP, PET_FRIGHT };

void pet_init(pet_t *p, int64_t now, int tz_min);
void pet_update(pet_t *p, int64_t now, int tz_min);
/* Returns a mood to react with (PIP_*), or -1. */
int pet_event(pet_t *p, int ev);

/* The base mood, saying why: asleep, a fright still felt, or calm. */
typedef struct { int kind; float value; } pet_why_t;
enum { PET_WHY_ASLEEP, PET_WHY_AFTER, PET_WHY_CALM };
int pet_mood_why(const pet_t *p, pet_why_t *why);
int pet_mood(const pet_t *p);

/* The knobs left: what counts as a bang, and how long a fright lasts. */
typedef struct {
    float loud_db, loud_jump, shock_db; /* a bang: this loud, this much over the room; shocked over shock_db */
    float after_fright;                 /* s */
} pet_tune_t;
extern pet_tune_t pet_tune;
typedef struct { const char *name; float *v; float min, max; } pet_knob_t;
extern const pet_knob_t pet_knobs[];
extern const int pet_knob_count;
float *pet_knob(const char *name); /* NULL if unknown */
int pet_hour(int64_t now, int tz_min);
