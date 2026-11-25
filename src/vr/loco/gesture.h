/*
 * Ability gestures.
 *
 * Turns hand motion into the N64 buttons the game's moves already listen for, so the game's own
 * move logic (owned moves, ammo, pads, cooldowns) stays in charge. Combos come from the decomp's
 * player input tables (src/global/E4090_player_input.c in the DK64 decompilation):
 *
 *   Z pressed in the air ............ Simian Slam (playerInputCanSlam)
 *   B ............................... attack / punch; with the gun out, fire
 *   Z held + B ...................... the Kong's special (Chimpy Charge, Primate Punch, ...)
 *   Z held + C-left ................. draw / holster the gun (crouch handler, slot 13)
 *   Z held + C-right ................ throw an orange (crouch handler, slot 14)
 *   Z held + C-up ................... play the instrument (playerInputCheckInstrumentPlayback)
 *   A held in the air ............... Pony Tail Twirl and the other "hold A" glides
 *   Z held on the ground ............ crouch (pads, high jump with A)
 *
 * Same rules as gtl.c: C99, no libc, floats only. Positions are metres in the play space
 * (OpenXR: x right, y up, z back), velocities in metres per second.
 */
#ifndef GST_H
#define GST_H

#ifdef __cplusplus
extern "C" {
#endif

/* N64 pad bits */
enum {
    GST_N64_CR = 0x0001,
    GST_N64_CL = 0x0002,
    GST_N64_CD = 0x0004,
    GST_N64_CU = 0x0008,
    GST_N64_R = 0x0010,
    GST_N64_L = 0x0020,
    GST_N64_START = 0x1000,
    GST_N64_Z = 0x2000,
    GST_N64_B = 0x4000,
    GST_N64_A = 0x8000
};

/* What fired this frame (gst_output.events) */
enum {
    GST_EV_SLAM = 1 << 0,
    GST_EV_PUNCH = 1 << 1,
    GST_EV_SPECIAL = 1 << 2,
    GST_EV_GUN = 1 << 3,
    GST_EV_FIRE = 1 << 4,
    GST_EV_ORANGE = 1 << 5,
    GST_EV_INSTRUMENT = 1 << 6,
    GST_EV_TWIRL = 1 << 7,
    GST_EV_CROUCH = 1 << 8,
    GST_EV_CLIMB = 1 << 9
};

typedef struct gst_params {
    int dominant;              /* 0 left, 1 right: gun hand; the other hand throws oranges */
    int crouch_by_ducking;     /* ducking holds Z */
    float duck_depth;          /* metres below standing height that count as ducking */
    float slam_speed;          /* both hands down this fast in the air */
    float punch_speed;         /* forward hand speed for a punch */
    float throw_speed;         /* release speed for an orange */
    float twirl_width;         /* hands this far out to each side */
    float climb_gain;          /* stick per m/s of hand-over-hand pull */
    int pair_frames;           /* frames to wait for the second hand of a double punch */
} gst_params;

typedef struct gst_input {
    float head[3];
    float head_yaw;            /* radians, counter-clockwise from above */
    float hand[2][3];
    float hand_vel[2][3];
    float grip[2];
    float trigger[2];
    int airborne;
    int in_water;
    int gun_out;               /* the game says the gun is drawn */
    int touching[2];           /* the locomotion core has this hand stuck to something */
    float dt;
} gst_input;

typedef struct gst_output {
    unsigned hold;             /* N64 buttons to hold this frame */
    unsigned events;           /* GST_EV_* that started this frame */
    int stick_valid;
    float stick[2];            /* -1..1, y up = forward / climb up */
    int aim_valid;
    float aim_yaw;             /* radians, play-space yaw the player should face for the move */
} gst_output;

#define GST_SEQ_MAX 4

typedef struct gst_step {
    unsigned mask;
    int frames;
} gst_step;

typedef struct gst_state {
    gst_step seq[GST_SEQ_MAX];
    int seq_len, seq_pos, seq_left;
    float cooldown[10];        /* per GST_EV bit index, seconds */
    float prev_grip[2], prev_trigger[2];
    int orange_held;           /* off hand closed at the hip */
    int punch_wait[2];         /* frames since this hand punched, waiting for a pair (0 = none) */
    int instrument_frames;
    float standing_y;          /* tallest recent head height */
    int standing_seen;
    float aim_yaw;
    int aim_frames;
    float recent_vel[2][4][3];  /* last few frames of hand velocity, for throws */
    int recent_pos;
} gst_state;

void gst_default_params(gst_params *p);
void gst_reset(gst_state *s);
void gst_step_frame(gst_state *s, const gst_params *p, const gst_input *in, gst_output *out);

/* Hand position relative to the head in the head's yaw frame: x right, y up, z forward. */
void gst_local(const gst_input *in, const float v[3], int is_position, float out[3]);

#ifdef __cplusplus
}
#endif

#endif
