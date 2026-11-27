/*
 * Arm locomotion core.
 *
 * You move the way you do in Gorilla Tag: hands that touch the world stay where they touched,
 * and the body moves opposite to the hand. Letting go throws the body with the hand speed of the
 * last few frames. There is no walking.
 *
 * This file is plain C99 with no libc. It is built three ways:
 *   - natively for the unit tests (src/vr/loco/tests),
 *   - inside the game patches (patches/vr_loco_glue.c includes gtl.c) for MIPS recompilation,
 *   - natively in the host when we want a preview without the game.
 * So: floats only, no malloc, no stdio, no stdlib. The caller supplies sqrt.
 *
 * Units are game units (DK64 uses roughly 15 units per metre at the default world scale).
 * The caller converts the headset's metres into game units before calling gtl_step.
 */
#ifndef GTL_H
#define GTL_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GTL_HISTORY_MAX
#define GTL_HISTORY_MAX 16
#endif

typedef struct gtl_vec3 {
    float x, y, z;
} gtl_vec3;

/* A sphere sweep result. t is the fraction of the move done before touching (0..1). */
typedef struct gtl_hit {
    float t;
    gtl_vec3 normal;
    float slip;    /* 0 = full grip, 1 = ice. Set by the world from its surface type. */
    int surface;   /* opaque surface id, passed through to the game for sounds / triggers */
} gtl_hit;

typedef struct gtl_world {
    void *user;
    /* Sweep a sphere of radius r from `from` to `to`. Return nonzero on contact and fill hit.
     * Must not report a contact when the sphere starts touching and moves away from the surface. */
    int (*sweep)(void *user, gtl_vec3 from, gtl_vec3 to, float r, gtl_hit *hit);
    float (*sqrt_fn)(float);
} gtl_world;

typedef struct gtl_params {
    float body_radius;
    float hand_radius;
    float arm_length;          /* hands never get further than this from the head */
    float head_height;         /* head sits this far above the body sphere's centre */

    float jump_multiplier;     /* release speed scale */
    float max_jump_speed;      /* units per second */
    float min_launch_speed;    /* below this, letting go just drops you */
    int history_frames;        /* frames averaged for the launch velocity, 1..GTL_HISTORY_MAX */

    float gravity;             /* units/s^2, positive number, applied along -y */
    float terminal_velocity;   /* max falling speed */
    float ground_friction;     /* 1/s, how fast horizontal speed dies when the body rests on ground */
    float ground_normal_y;     /* surfaces with normal.y above this count as ground */

    float unstick_distance;    /* a stuck hand lets go once the controller is this far from it */
    float slip_threshold;      /* surfaces with slip >= this never grip */

    /* Water: MonkeSwim's stroke (github.com/AHauntedArmy/MonkeSwim, GPL-3.0, SwimManager.cs and
     * SwimHandTracker.cs). A hand moving through the water while it strokes pushes the body the
     * opposite way, by swim_accel x its speed; the stroke's direction is its running average
     * since the stroke began (so a wobble mid-stroke doesn't steer), leaned towards the look.
     * Speed only ever builds towards swim_max_speed along the push (per axis, like its
     * TryUpdateAndClamp), and the water takes off a constant swim_resistance: you glide. */
    float swim_accel;          /* 1/s: velocity gained per second per (hand speed); MonkeSwim's
                                  1.3 x 0.01 per physics step, at 90 steps a second */
    float swim_resistance;     /* units/s^2, constant slow-down (MonkeSwim 0.2 m/s^2) */
    float swim_max_speed;      /* units/s, strokes add speed only up to this (MonkeSwim 6.5 m/s) */
    float paddle_look;         /* each stroke leans this much (fraction) towards where you
                                  look, so swimming heads where you face (MonkeSwim: 0.2) */
    float paddle_deadzone;     /* hand speeds below this (units/s) don't stroke (tracking noise) */
    float buoyancy;            /* units/s^2 upwards while in water, offsets gravity */

    int max_slide_iterations;
} gtl_params;

enum {
    GTL_MODE_AIR_GROUND = 0,   /* normal: gravity, hands grip surfaces */
    GTL_MODE_WATER = 1,        /* swimming: no gravity, hands paddle */
    GTL_MODE_FROZEN = 2        /* cutscenes etc: hands follow controllers, body does not move */
};

/* Tracked input, already converted to game units and rotated into the world's yaw.
 * Positions are relative to the play space origin, not absolute. */
typedef struct gtl_input {
    gtl_vec3 head;
    gtl_vec3 hand[2];
    int hand_enabled[2];       /* 0 while a hand is busy (holding a gun, an instrument...) */
    int hand_hold[2];          /* the grip holds this hand where it is, surface or not (a tree, a
                                  ladder, a vine the caller says is in reach); pulls move the body */
    gtl_vec3 look;             /* where the head faces, unit length (zero: no look assist) */
    int hand_stroke[2];        /* water: this hand strokes now (MonkeSwim: trigger held; ours: the
                                  grip). The stroke's direction averages from when it began */
    int stroke_always;         /* water: every hand movement strokes, no button (each frame its
                                  own direction, so even strokes back and forth cancel) */
    int mode;
    int external_flight;       /* the game owns gravity and free flight: when no hand holds,
                                  only room-scale moves the body and a release just reports
                                  the launch velocity */
    float dt;
} gtl_input;

typedef struct gtl_hand {
    gtl_vec3 pos;              /* where the hand is drawn, world space */
    int touching;
    int was_touching;
    float slip;
    int surface;
    gtl_vec3 normal;
    gtl_vec3 last_ctrl;        /* controller last step (play space), for swimming */
    int stroking;              /* a swim stroke is going on */
    gtl_vec3 stroke_sum;       /* its direction so far (MonkeSwim's smoothed direction) */
} gtl_hand;

typedef struct gtl_state {
    gtl_vec3 origin;           /* world position of the play space origin */
    gtl_vec3 body;             /* world position of the head / body sphere */
    gtl_vec3 velocity;         /* free-flight velocity, units per second */
    gtl_hand hand[2];
    int grounded;
    int initialised;
    gtl_vec3 last_head;        /* tracked head last step (play space) */

    gtl_vec3 last_end;         /* body at the end of the last step, for the velocity history */
    int has_last_end;
    /* Body velocity over the last few steps, whatever moved it: hands, flight, the game. Like
     * Gorilla Tag's, it carries through the air, so pushing off again while already moving adds
     * to that speed instead of starting from zero. */
    gtl_vec3 history[GTL_HISTORY_MAX];
    int history_count;
    int history_head;
} gtl_state;

/* What happened this step. Useful for haptics and the stats overlay. */
typedef struct gtl_events {
    int hand_contact[2];       /* hand touched something this step (rising edge) */
    float contact_speed[2];    /* how fast the hand was moving when it touched */
    int hand_release[2];
    int launched;
    gtl_vec3 launch_velocity;
    int landed;
} gtl_events;

void gtl_default_params(gtl_params *p);

/* Put the body at a world position (spawning, loading zones, warps, cutscene end). */
void gtl_reset(gtl_state *s, gtl_vec3 body_world, const gtl_input *in);

void gtl_step(gtl_state *s, const gtl_params *p, const gtl_world *w, const gtl_input *in,
              gtl_events *ev);

/* Average of the velocity history, the speed you would launch with right now (before the
 * multiplier). For the speed overlay. */
gtl_vec3 gtl_history_velocity(const gtl_state *s, const gtl_params *p);

#ifdef __cplusplus
}
#endif

#endif
