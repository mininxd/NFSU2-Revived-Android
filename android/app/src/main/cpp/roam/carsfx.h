/* carsfx.h -- friends' engines, nitrous and hydraulics, heard: the game's own
 * sounds (SOUND/ENGINE, SOUND/NOS) played at each friend's car over the
 * game's sound (carsfx.c).  Free roam (roam_game.c) says what each friend's
 * car does; the mixers (audio.m, audio_web.c) add the voices to their mix. */
#ifndef CARSFX_H
#define CARSFX_H
#include <stdint.h>

#define CARSFX_VOICES 32

/* what a friend's car does now, as the game's own sound of a car reads it
 * from the car (0x484F20, 0x47DD40), and its engine as the game sounds the
 * player's own (CARSFX_PlayerEngine) */
typedef struct {
    uint8_t nos;                     /* its nitrous is on */
    uint8_t purge;                   /* ...and let out: standing (under 5 mph) or off the throttle */
    uint8_t hyd;                     /* each wheel's hydraulics, 2 bits from bit 2 k (0x5ED470): 0 down, 1 up, 2 rising, 3 lowering */
    uint8_t thumps;                  /* how many times it landed on its hydraulics (counts on, wraps) */
    uint8_t game_boost;              /* the game sounds its nitrous itself, but not letting it out (an AI racer's sound state) */
    const char *gin[2];              /* its engine's sound: two GIN files in SOUND\ENGINE, the engine pulling and letting off
                                        (the game's own for its car and engine upgrades); 0: no engine heard */
    float eng;                       /* ...its rpm on the sound's scale: 1000 at idle, 10000 at the most */
    float eng_gain[2];               /* ...each layer's volume, as the game plays the player's own (1: the most) */
} carsfx_state;

/* A frame on the game thread: carsfx_begin with the listener (the camera's
 * position and forward, the player's car's velocity, m and m/s; pos 0:
 * nothing is heard), carsfx_car for every friend's car heard (who: one per
 * car; p, v: where it is and how it moves), then carsfx_end: a car not
 * given since carsfx_begin falls silent.  The first carsfx_begin reads the
 * game's nitrous and hydraulics banks, a car's first engine its GIN files,
 * each on a thread of its own. */
void carsfx_begin(const float *pos, const float *fwd, const float *vel);
void carsfx_car(uint32_t who, const char *name, const float *p, const float *v, const carsfx_state *st);
void carsfx_end(void);
/* Adds the voices to an interleaved stereo float mix.  Mixer thread. */
void carsfx_mix(float *acc, unsigned frames, unsigned rate);
#endif
