/* Pip: the character screen. Portable LVGL code, shared by the simulator and the firmware. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

#include "moods.h"

enum { PIP_BTN_PWR, PIP_BTN_BOOT };

typedef struct {
    const char *mood;     /* what the face shows now */
    const char *reaction; /* "" when none */
    int steps;
    float tilt_x, tilt_y, shake;
    bool face_down;
    float frame_ms; /* face update + render, smoothed */
    bool asleep, auto_mood;
    int hour;
} pip_info_t;

void pip_create(lv_obj_t *parent);
void pip_set_mood(int mood);                      /* pin a mood by hand (turns the automatic one off) */
void pip_auto(bool on);                           /* the calm base mood (asleep, a fright, else neutral) */
void pip_time(int64_t unix_s, int tz_min);        /* wall clock, call often; the pet catches up any gap */
/* The pet state to persist, and restoring it (false if it is from another version). */
const void *pip_pet_state(size_t *size);
bool pip_pet_restore(const void *data, size_t size);
/* Things he reacts to, each can be switched off (the debug page, filming) */
enum { PIP_F_SHAKE, PIP_F_BLOW, PIP_F_FACEDOWN, PIP_F_VOICE, PIP_F_BATTERY, PIP_F_TIME, PIP_F_NOISE, PIP_F_COUNT };
void pip_feature(int f, bool on);
bool pip_feature_on(int f);
void *pip_pet(void); /* the live pet_t (pet.h), for the debug page */
void pip_dev(bool on);                    /* developer mode: a wireframe of the eyes and a HUD over the face (for filming) */
bool pip_dev_on(void);
void pip_dev_note(const char *line);      /* the host's line for the HUD: memory, the panel */
void pip_power(int seconds);             /* fall asleep (screen off, no drawing) after this long left alone; 0 = never */
int pip_power_after(void);
void pip_hold_awake(bool on);              /* an app that is there to be looked at keeps him awake */
int pip_display_level(void);               /* the panel: 0 off, 25 dimmed (about to sleep), 100 */
void pip_eco(bool on);                     /* energy saving: fewer frames, the eyes ignore the tilt, no listening for breath or bangs */
bool pip_eco_on(void);
bool pip_mic_wanted(void);                 /* Pip listens: the host may switch the microphone off when he does not */
void pip_wake(void);                       /* something to look at: wake up */
void pip_wake_up(void);                   /* power-on: eyes open out of black, sleepy, a shake, playful */
void pip_shake_ok(bool ok);                /* false while an app wants the shakes for itself */
void pip_battery(int pct, bool charging); /* plugged in he cheers up for a moment */
void pip_pet_rebase(int64_t now); /* the clock jumped (a made-up time replaced by the real one): do not age him for it */
void pip_imu(float ax, float ay, float az, float dt); /* accelerometer in g, device axes, see imu.h */
void pip_button(int id);                          /* short press of a side button */
bool pip_display_on(void);                        /* false: lying face down, the screen may go dark (nothing is drawn) */
void pip_seen(void);                              /* someone touched him or pressed a button: wake the screen */
enum { PIP_CALL_ANSWERED, PIP_CALL_DECLINED, PIP_CALL_MISSED, PIP_PAIRED };
void pip_phone(int what);                         /* PIP_CALL_*: how a call went, the phone paired */
void pip_note(void);                              /* a new notification: wake the screen */
void pip_get_info(pip_info_t *out);
const char *pip_mood_name(int mood);
void pip_react(int mood, float secs, const char *name); /* show a mood for a while, e.g. when the timer rings */
void pip_glance(float x, float y, float secs);         /* look at a screen point for a while (0 secs: stop) */
void pip_on_swipe(void (*cb)(int dir));                  /* a quick flick on the face: dir -1 left, +1 right, 2 down from the top */
int pip_find_mood(const char *name); /* -1 if unknown */
/* the slowest frame of the last secs seconds (ms, 0: none drawn), and what he showed then (mood, reaction or "") */
float pip_frame_worst(int secs, const char **mood, const char **reaction);
/* Debug keys: '1'..'9','0','-','=' moods, ' ' next mood, 'b' blink */
void pip_key(char k);
