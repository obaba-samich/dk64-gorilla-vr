#include <stdio.h>
#include <string.h>
#include "fake_world.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

#define DT (1.0f / 90.0f)

typedef struct rig {
    fake_world fw;
    gtl_world w;
    gtl_params p;
    gtl_state s;
    gtl_input in;
    gtl_events ev;
    int launches;
    int contacts;
} rig;

static gtl_vec3 V(float x, float y, float z) { gtl_vec3 v; v.x = x; v.y = y; v.z = z; return v; }

static void rig_init(rig *r, gtl_vec3 left, gtl_vec3 right) {
    memset(r, 0, sizeof(*r));
    gtl_default_params(&r->p);
    r->w = fw_world(&r->fw);
    r->in.head = V(0, 0, 0);
    r->in.hand[0] = left;
    r->in.hand[1] = right;
    r->in.hand_enabled[0] = r->in.hand_enabled[1] = 1;
    r->in.dt = DT;
    r->in.mode = GTL_MODE_AIR_GROUND;
}

static void rig_start(rig *r, gtl_vec3 body) {
    r->w = fw_world(&r->fw);
    gtl_reset(&r->s, body, &r->in);
}

static void rig_step(rig *r, int n, gtl_vec3 dl, gtl_vec3 dr, gtl_vec3 dhead) {
    int i;
    for (i = 0; i < n; i++) {
        r->in.head = V(r->in.head.x + dhead.x, r->in.head.y + dhead.y, r->in.head.z + dhead.z);
        r->in.hand[0] = V(r->in.hand[0].x + dl.x + dhead.x, r->in.hand[0].y + dl.y + dhead.y,
                          r->in.hand[0].z + dl.z + dhead.z);
        r->in.hand[1] = V(r->in.hand[1].x + dr.x + dhead.x, r->in.hand[1].y + dr.y + dhead.y,
                          r->in.hand[1].z + dr.z + dhead.z);
        gtl_step(&r->s, &r->p, &r->w, &r->in, &r->ev);
        r->launches += r->ev.launched;
        r->contacts += r->ev.hand_contact[0] + r->ev.hand_contact[1];
    }
}

static gtl_vec3 Z3 = { 0, 0, 0 };

/* Hands held out to the sides at shoulder height, clear of everything. */
#define HANDS_OUT V(-8, -2, 4), V(8, -2, 4)

static void test_falls_and_rests_on_floor(void) {
    rig r;
    rig_init(&r, HANDS_OUT);
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 30, 0));
    rig_step(&r, 270, Z3, Z3, Z3);
    CHECK(fabsf(r.s.body.y - r.p.body_radius) < 0.1f, "body y %f", r.s.body.y);
    CHECK(r.s.grounded, "should be grounded");
    CHECK(fabsf(r.s.velocity.y) < 0.01f, "vy %f", r.s.velocity.y);
}

/* Settle on the floor with the left hand resting on it. */
static void setup_hand_on_floor(rig *r) {
    rig_init(r, V(0, -1, 5), V(8, 0, 4));
    r->fw.has_floor = 1;
    rig_start(r, V(0, 4, 0));
    rig_step(r, 30, Z3, Z3, Z3);
    /* lower the left hand onto the floor and a bit past it */
    rig_step(r, 10, V(0, -0.3f, 0), Z3, Z3);
}

static void test_hand_touches_and_stays_on_surface(void) {
    rig r;
    setup_hand_on_floor(&r);
    CHECK(r.s.hand[0].touching, "left hand should touch the floor");
    CHECK(r.contacts == 1, "one contact event, got %d", r.contacts);
    CHECK(r.s.hand[0].pos.y >= r.p.hand_radius - 0.05f, "hand went into the floor: %f", r.s.hand[0].pos.y);
    CHECK(!r.s.hand[1].touching, "right hand is in the air");
}

static void test_push_down_lifts_body(void) {
    rig r;
    float y0;
    setup_hand_on_floor(&r);
    y0 = r.s.body.y;
    rig_step(&r, 10, V(0, -0.3f, 0), Z3, Z3);
    CHECK(fabsf((r.s.body.y - y0) - 3.0f) < 0.2f, "body should rise 3, rose %f", r.s.body.y - y0);
    CHECK(r.s.hand[0].touching, "still touching while pushing");
    CHECK(r.s.hand[0].pos.y >= r.p.hand_radius - 0.05f, "hand y %f", r.s.hand[0].pos.y);
}

static void test_push_back_moves_forward(void) {
    rig r;
    float z0;
    setup_hand_on_floor(&r);
    z0 = r.s.body.z;
    /* drag the hand backwards (towards -z) while pressing slightly down */
    rig_step(&r, 10, V(0, -0.02f, -0.5f), Z3, Z3);
    CHECK(r.s.body.z - z0 > 4.5f, "should move forward ~5, moved %f", r.s.body.z - z0);
    CHECK(r.s.hand[0].touching, "hand keeps contact");
}

static void test_hand_stays_put_while_stuck(void) {
    rig r;
    gtl_vec3 h0;
    setup_hand_on_floor(&r);
    h0 = r.s.hand[0].pos;
    rig_step(&r, 10, V(0.2f, -0.05f, -0.3f), Z3, Z3);
    CHECK(fabsf(r.s.hand[0].pos.x - h0.x) < 0.01f && fabsf(r.s.hand[0].pos.z - h0.z) < 0.01f,
          "stuck hand drifted to %f %f from %f %f", r.s.hand[0].pos.x, r.s.hand[0].pos.z, h0.x, h0.z);
}

static void test_lift_off_releases_without_dragging(void) {
    rig r;
    float y0;
    setup_hand_on_floor(&r);
    y0 = r.s.body.y;
    rig_step(&r, 1, V(0, 1.0f, 0), Z3, Z3);
    CHECK(!r.s.hand[0].touching, "hand should let go");
    CHECK(r.s.body.y >= y0 - 0.05f, "lifting the hand pulled the body down: %f -> %f", y0, r.s.body.y);
}

static void test_fast_push_launches(void) {
    rig r;
    float peak = 0.0f;
    int i;
    setup_hand_on_floor(&r);
    r.launches = 0;
    rig_step(&r, 6, V(0, -2.0f, 0), Z3, Z3); /* 180 units/s down stroke */
    rig_step(&r, 1, V(0, 3.0f, 0), Z3, Z3);  /* snap the hand back up */
    CHECK(r.launches == 1, "expected one launch, got %d", r.launches);
    CHECK(r.s.velocity.y > 100.0f, "launch vy %f", r.s.velocity.y);
    {
        float sp = sqrtf(r.s.velocity.x * r.s.velocity.x + r.s.velocity.y * r.s.velocity.y +
                         r.s.velocity.z * r.s.velocity.z);
        CHECK(sp <= r.p.max_jump_speed + 0.01f, "launch speed %f over cap", sp);
    }
    for (i = 0; i < 90; i++) {
        rig_step(&r, 1, Z3, Z3, Z3);
        if (r.s.body.y > peak) peak = r.s.body.y;
    }
    CHECK(peak > 40.0f, "jump peak only %f", peak);
}

static void test_multiplier_and_cap(void) {
    rig r;
    setup_hand_on_floor(&r);
    r.p.max_jump_speed = 1000.0f;
    r.p.jump_multiplier = 2.0f;
    rig_step(&r, 6, V(0, -1.0f, 0), Z3, Z3); /* 90 u/s */
    rig_step(&r, 1, V(0, 3.0f, 0), Z3, Z3);
    CHECK(fabsf(r.s.velocity.y - 180.0f) < 5.0f, "2x of 90 should be 180, got %f", r.s.velocity.y);

    setup_hand_on_floor(&r);
    r.p.max_jump_speed = 50.0f;
    r.p.unstick_distance = 20.0f;   /* the game's proportions: a push faster than the cap stays held */
    rig_step(&r, 6, V(0, -1.0f, 0), Z3, Z3);
    rig_step(&r, 1, V(0, 3.0f, 0), Z3, Z3);
    CHECK(fabsf(r.s.velocity.y - 50.0f) < 0.5f, "capped at 50, got %f", r.s.velocity.y);
}

static void test_slow_release_does_not_launch(void) {
    rig r;
    setup_hand_on_floor(&r);
    r.launches = 0;
    rig_step(&r, 6, V(0, -0.05f, 0), Z3, Z3); /* 4.5 u/s, below min_launch_speed */
    rig_step(&r, 1, V(0, 3.0f, 0), Z3, Z3);
    CHECK(r.launches == 0, "gentle release should not launch");
}

static void test_history_length_matters(void) {
    rig a, b;
    setup_hand_on_floor(&a);
    a.p.history_frames = 2;
    a.p.max_jump_speed = 1000.0f;
    rig_step(&a, 6, V(0, -0.2f, 0), Z3, Z3);
    rig_step(&a, 2, V(0, -2.0f, 0), Z3, Z3);
    rig_step(&a, 1, V(0, 3.0f, 0), Z3, Z3);

    setup_hand_on_floor(&b);
    b.p.history_frames = 8;
    b.p.max_jump_speed = 1000.0f;
    rig_step(&b, 6, V(0, -0.2f, 0), Z3, Z3);
    rig_step(&b, 2, V(0, -2.0f, 0), Z3, Z3);
    rig_step(&b, 1, V(0, 3.0f, 0), Z3, Z3);
    CHECK(a.s.velocity.y > b.s.velocity.y + 50.0f,
          "short history should favour the fast end of the stroke: %f vs %f", a.s.velocity.y, b.s.velocity.y);
}

/* Gorilla Tag: the velocity history follows the body through the air, so pushing off while
 * already moving keeps that speed (times the multiplier) instead of starting from the stroke. */
static void momentum_run(rig *r, float start_vx) {
    setup_hand_on_floor(r);
    r->p.max_jump_speed = 1000.0f;
    r->p.ground_friction = 0.0f;
    rig_step(r, 1, V(0, 3.0f, 0), Z3, Z3);   /* hand off the floor */
    r->s.velocity.x = start_vx;              /* sliding along, nothing held */
    rig_step(r, 8, Z3, Z3, Z3);
    rig_step(r, 1, V(0, -3.3f, 0), Z3, Z3);  /* slap the floor */
    rig_step(r, 2, V(0, -1.0f, 0), Z3, Z3);  /* short push */
    r->launches = 0;
    rig_step(r, 1, V(0, 3.0f, 0), Z3, Z3);   /* let go */
}

static void test_momentum_carries(void) {
    rig a, b;
    momentum_run(&a, 120.0f);
    momentum_run(&b, 0.0f);
    CHECK(a.launches == 1, "moving body should launch, got %d", a.launches);
    CHECK(a.s.velocity.x > 50.0f && a.s.velocity.x > b.s.velocity.x + 50.0f,
          "sideways speed should carry into the push: %f vs %f", a.s.velocity.x, b.s.velocity.x);
}

/* Gorilla Tag: the head moves inside the body. Bobbing up and down doesn't lift the body off the
 * floor, and crouching brings the hands (which hang from the real head) closer to the ground. */
static void test_head_bob_stays_inside(void) {
    rig r;
    float y0;
    int i;
    rig_init(&r, V(-3, -1, 0), V(3, -1, 0));
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 30, Z3, Z3, Z3);
    y0 = r.s.body.y;
    for (i = 0; i < 10; i++) {
        rig_step(&r, 3, Z3, Z3, V(0, 0.5f, 0));   /* head up 1.5, hands with it */
        rig_step(&r, 3, Z3, Z3, V(0, -0.5f, 0));
    }
    CHECK(fabsf(r.s.body.y - y0) < 0.05f, "head bobbing moved the body %f", r.s.body.y - y0);
    /* hands 1 under the head don't reach the floor standing; crouch 2 and they do */
    CHECK(!r.s.hand[0].touching, "standing: hands in the air");
    rig_step(&r, 4, Z3, Z3, V(0, -0.5f, 0));
    CHECK(r.s.hand[0].touching && r.s.hand[1].touching, "crouched: hands on the floor");
    CHECK(fabsf(r.s.body.y - y0) < 0.5f, "crouching to the floor kept the body there: %f", r.s.body.y - y0);
}

/* A smack into the ground lifts the body no faster than the throw cap per step. */
static void test_smack_rises_smoothly(void) {
    rig r;
    float y0, y1;
    rig_init(&r, V(-3, -1, 0), V(3, -1, 0));
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 30, Z3, Z3, Z3);
    rig_step(&r, 6, V(0, -0.3f, 0), V(0, -0.3f, 0), Z3);   /* hands down to the floor */
    r.p.unstick_distance = 20.0f;   /* the game's proportions */
    y0 = r.s.body.y;
    rig_step(&r, 1, V(0, -6.0f, 0), V(0, -6.0f, 0), Z3);   /* a smack: 6 units in one step */
    y1 = r.s.body.y;
    CHECK(y1 - y0 <= r.p.max_jump_speed * r.in.dt + 0.01f, "one step rose %f (cap %f)", y1 - y0, r.p.max_jump_speed * r.in.dt);
    rig_step(&r, 6, Z3, Z3, Z3);
    CHECK(r.s.body.y - y0 > 4.0f, "and the rest of the push follows: %f", r.s.body.y - y0);
}

static void test_wall_climb(void) {
    rig r;
    float y0;
    /* wall in front: z from 10 to 20, tall */
    rig_init(&r, V(-3, 2, 6), V(3, 2, 6));
    r.fw.has_floor = 1;
    fw_add_box(&r.fw, -50, 0, 10, 50, 200, 20, 0.0f);
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 10, Z3, Z3, Z3);
    rig_step(&r, 10, V(0, 0, 0.5f), V(0, 0, 0.5f), Z3); /* reach into the wall */
    CHECK(r.s.hand[0].touching && r.s.hand[1].touching, "both hands on the wall");
    CHECK(r.s.hand[0].pos.z <= 10.0f - r.p.hand_radius + 0.05f, "hand inside wall: %f", r.s.hand[0].pos.z);
    y0 = r.s.body.y;
    /* pull both hands down, pressing into the wall a little */
    rig_step(&r, 20, V(0, -0.5f, 0.01f), V(0, -0.5f, 0.01f), Z3);
    CHECK(r.s.body.y - y0 > 9.0f, "pull-down should climb ~10, climbed %f", r.s.body.y - y0);
    /* hand over hand: release one, reach up, grab, pull */
    rig_step(&r, 1, V(0, 0, -1.0f), Z3, Z3);
    CHECK(!r.s.hand[0].touching && r.s.hand[1].touching, "left off, right holding");
    rig_step(&r, 10, V(0, 1.0f, 0), Z3, Z3);
    rig_step(&r, 5, V(0, 0, 0.4f), Z3, Z3);
    CHECK(r.s.hand[0].touching, "left grabbed again");
    CHECK(r.s.body.y - y0 > 9.0f, "held our height while switching hands: %f", r.s.body.y - y0);
}

static void test_body_does_not_pass_walls(void) {
    rig r;
    int i;
    rig_init(&r, V(0, -1, 5), V(8, 0, 4));
    r.fw.has_floor = 1;
    fw_add_box(&r.fw, -50, 0, 30, 50, 200, 40, 0.0f);
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 10, Z3, Z3, Z3);
    rig_step(&r, 10, V(0, -0.3f, 0), Z3, Z3);
    /* row along the floor towards the wall at z=30, far more strokes than needed to get there.
     * Near the wall the reaching hand lands on it and pushes us back, which is fine. */
    for (i = 0; i < 10; i++) {
        rig_step(&r, 10, V(0, -0.02f, -0.6f), Z3, Z3);
        rig_step(&r, 1, V(0, 2.0f, 0), Z3, Z3);
        rig_step(&r, 10, V(0, 0, 0.6f), Z3, Z3);
        rig_step(&r, 1, V(0, -2.0f, 0), Z3, Z3);
    }
    CHECK(r.s.body.z <= 30.0f - r.p.body_radius + 0.05f, "body went into the wall: z %f", r.s.body.z);
    CHECK(r.s.body.z > 15.0f, "should have made progress: z %f", r.s.body.z);
}

static void test_room_scale_walk_collides(void) {
    rig r;
    /* arms at the sides; with arms out front the hands meet the wall first and push us off it */
    rig_init(&r, V(-8, -2, 0), V(8, -2, 0));
    r.fw.has_floor = 1;
    fw_add_box(&r.fw, -50, 0, 8, 50, 200, 20, 0.0f);
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 60, Z3, Z3, V(0, 0, 0.2f)); /* walk 12 units in the room */
    CHECK(r.s.body.z <= 8.0f - r.p.body_radius + 0.05f, "walked through the wall: %f", r.s.body.z);
    CHECK(r.s.body.z > 3.0f, "room walk should still move us up to the wall: %f", r.s.body.z);
}

static void test_slippery_surface(void) {
    rig a, b;
    float za, zb;
    setup_hand_on_floor(&a);
    za = a.s.body.z;
    rig_step(&a, 10, V(0, -0.02f, -0.5f), Z3, Z3);
    za = a.s.body.z - za;

    rig_init(&b, V(0, -1, 5), V(8, 0, 4));
    b.fw.has_floor = 1;
    b.fw.floor_slip = 0.5f;
    rig_start(&b, V(0, 4, 0));
    rig_step(&b, 30, Z3, Z3, Z3);
    rig_step(&b, 10, V(0, -0.3f, 0), Z3, Z3);
    zb = b.s.body.z;
    rig_step(&b, 10, V(0, -0.02f, -0.5f), Z3, Z3);
    zb = b.s.body.z - zb;
    CHECK(zb < za * 0.75f && zb > 0.5f, "half grip should move less: %f vs %f", zb, za);
}

static void test_ice_never_grips(void) {
    rig r;
    rig_init(&r, V(0, -1, 5), V(8, 0, 4));
    r.fw.has_floor = 1;
    r.fw.floor_slip = 1.0f;
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 30, Z3, Z3, Z3);
    rig_step(&r, 10, V(0, -0.3f, 0), Z3, Z3);
    CHECK(!r.s.hand[0].touching, "ice should not grip");
    CHECK(r.s.hand[0].pos.y >= r.p.hand_radius - 0.05f, "but the hand still can't go through it: %f",
          r.s.hand[0].pos.y);
}

static void test_two_hands_average(void) {
    rig r;
    float z0;
    rig_init(&r, V(-3, -1, 5), V(3, -1, 5));
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 30, Z3, Z3, Z3);
    rig_step(&r, 10, V(0, -0.3f, 0), V(0, -0.3f, 0), Z3);
    CHECK(r.s.hand[0].touching && r.s.hand[1].touching, "both down");
    z0 = r.s.body.z;
    /* only the left hand rows, the right just rests */
    rig_step(&r, 4, V(0, -0.02f, -0.5f), V(0, -0.02f, 0), Z3);
    CHECK(fabsf((r.s.body.z - z0) - 1.0f) < 0.3f, "averaged pull should be half (1.0), got %f", r.s.body.z - z0);
}

static void test_arm_length_clamp(void) {
    rig r;
    float d;
    rig_init(&r, V(0, 0, 100), V(0, 0, -100));
    rig_start(&r, V(0, 50, 0));
    rig_step(&r, 1, Z3, Z3, Z3);
    d = sqrtf((r.s.hand[0].pos.x - r.s.body.x) * (r.s.hand[0].pos.x - r.s.body.x) +
              (r.s.hand[0].pos.y - r.s.body.y) * (r.s.hand[0].pos.y - r.s.body.y) +
              (r.s.hand[0].pos.z - r.s.body.z) * (r.s.hand[0].pos.z - r.s.body.z));
    CHECK(fabsf(d - r.p.arm_length) < 0.01f, "hand at %f from head, arm %f", d, r.p.arm_length);
}

static void test_disabled_hand_does_not_grip(void) {
    rig r;
    setup_hand_on_floor(&r);
    r.in.hand_enabled[0] = 0;
    rig_step(&r, 1, V(0, -0.3f, 0), Z3, Z3);
    CHECK(!r.s.hand[0].touching, "busy hand should not hold");
}

static void test_water_paddle(void) {
    rig r;
    int i;
    rig_init(&r, V(-4, -2, 6), V(4, -2, 6));
    r.in.mode = GTL_MODE_WATER;
    rig_start(&r, V(0, 50, 0));
    /* breaststroke: fast push back, slow reach forward */
    for (i = 0; i < 6; i++) {
        rig_step(&r, 8, V(0, 0, -1.0f), V(0, 0, -1.0f), Z3);
        rig_step(&r, 32, V(0, 0, 0.25f), V(0, 0, 0.25f), Z3);
    }
    CHECK(r.s.body.z > 10.0f, "strokes should swim us forward, z %f", r.s.body.z);
    CHECK(fabsf(r.s.body.y - 50.0f) < 2.0f, "neutral buoyancy, y %f", r.s.body.y);
}

static void test_water_still_is_still(void) {
    rig r;
    rig_init(&r, HANDS_OUT);
    r.in.mode = GTL_MODE_WATER;
    rig_start(&r, V(0, 50, 0));
    rig_step(&r, 180, Z3, Z3, Z3);
    CHECK(fabsf(r.s.body.z) < 0.01f && fabsf(r.s.body.y - 50.0f) < 0.5f, "drifted to %f %f",
          r.s.body.y, r.s.body.z);
}

static void test_frozen_mode(void) {
    rig r;
    rig_init(&r, HANDS_OUT);
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 30, 0));
    r.in.mode = GTL_MODE_FROZEN;
    rig_step(&r, 90, V(0.1f, 0, 0), Z3, Z3);
    CHECK(fabsf(r.s.body.y - 30.0f) < 0.001f, "frozen body moved: %f", r.s.body.y);
    CHECK(fabsf(r.s.hand[0].pos.x - (-8.0f + 9.0f)) < 0.01f, "hand follows controller: %f", r.s.hand[0].pos.x);
}

static void test_reset_teleports(void) {
    rig r;
    rig_init(&r, HANDS_OUT);
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 10, Z3, Z3, Z3);
    gtl_reset(&r.s, V(100, 20, -40), &r.in);
    CHECK(r.s.body.x == 100.0f && r.s.body.z == -40.0f, "reset should place the body");
    CHECK(r.s.velocity.y == 0.0f, "reset clears velocity");
    rig_step(&r, 1, Z3, Z3, Z3);
    CHECK(fabsf(r.s.hand[0].pos.x - 92.0f) < 0.01f, "hands come along: %f", r.s.hand[0].pos.x);
}

/* After a pause the hands may be low, their targets inside the floor: they must end on its
 * surface, not stuck inside it. */
static void test_reset_with_hands_in_the_floor(void) {
    rig r;
    float y0;
    rig_init(&r, V(-3, -1, 0), V(3, -1, 0));
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 4, 0));
    rig_step(&r, 30, Z3, Z3, Z3);
    r.p.unstick_distance = 10.0f;  /* the game's proportions (26 units against a 1.2 hand) */
    r.in.hand[0].y = -6.5f;      /* under the floor (at y 0; the head is at 4) */
    r.in.hand[1].y = -6.5f;
    gtl_reset(&r.s, r.s.body, &r.in);
    y0 = r.s.body.y;
    rig_step(&r, 1, Z3, Z3, Z3);
    CHECK(r.s.hand[0].pos.y >= r.p.hand_radius - 0.05f, "hand on the floor, not in it: %f", r.s.hand[0].pos.y);
    CHECK(r.s.hand[0].touching, "and holding it");
    /* rowing still moves the body */
    rig_step(&r, 6, V(0, 0, 0.5f), V(0, 0, 0.5f), Z3);
    CHECK(r.s.body.z < -1.0f, "rowing after the reset moves us: z %f", r.s.body.z);
    (void)y0;
}

static void test_landing_event(void) {
    rig r;
    int landed = 0, i;
    rig_init(&r, HANDS_OUT);
    r.fw.has_floor = 1;
    rig_start(&r, V(0, 20, 0));
    for (i = 0; i < 120; i++) {
        rig_step(&r, 1, Z3, Z3, Z3);
        landed += r.ev.landed;
    }
    CHECK(landed == 1, "one landing, got %d", landed);
}

static void test_zero_dt_is_noop(void) {
    rig r;
    gtl_vec3 b;
    rig_init(&r, HANDS_OUT);
    rig_start(&r, V(0, 30, 0));
    b = r.s.body;
    r.in.dt = 0.0f;
    rig_step(&r, 5, Z3, Z3, Z3);
    CHECK(r.s.body.y == b.y, "dt 0 moved the body");
}

static void test_head_height(void) {
    rig r;
    rig_init(&r, V(0, -2, 5), V(8, -2, 0));
    r.p.head_height = 10.0f;
    rig_start(&r, V(0, 50, 0));
    rig_step(&r, 1, Z3, Z3, Z3);
    CHECK(fabsf(r.s.hand[0].pos.y - (r.s.body.y + 10.0f - 2.0f)) < 0.01f,
          "hands hang from the head, not the body centre: hand %f body %f", r.s.hand[0].pos.y, r.s.body.y);
}

static void test_external_flight(void) {
    rig r;
    int launched = 0, i;
    setup_hand_on_floor(&r);
    r.in.external_flight = 1;
    {
        float y0 = r.s.body.y;
        rig_step(&r, 1, V(0, 3.0f, 0), Z3, Z3); /* let go, nothing holding */
        for (i = 0; i < 30; i++) {
            rig_step(&r, 1, Z3, Z3, Z3);
        }
        CHECK(fabsf(r.s.body.y - y0) < 0.01f, "with the game owning flight the body stays put: %f -> %f", y0, r.s.body.y);
    }
    rig_step(&r, 1, V(0, -3.5f, 0), Z3, Z3);   /* back onto the floor */
    rig_step(&r, 6, V(0, -1.0f, 0), Z3, Z3);   /* push, 90 units/s */
    for (i = 0; i < 1; i++) {
        rig_step(&r, 1, V(0, 3.0f, 0), Z3, Z3);
        launched += r.ev.launched;
    }
    CHECK(launched == 1 && r.ev.launch_velocity.y > 80.0f, "release still reports the launch: %d %f", launched,
          r.ev.launch_velocity.y);
    CHECK(r.s.velocity.y == 0.0f || r.s.velocity.y > 0.0f, "velocity is the game's business");
}

int main(void) {
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "falls and rests on floor", test_falls_and_rests_on_floor },
        { "hand touches and stays on surface", test_hand_touches_and_stays_on_surface },
        { "push down lifts body", test_push_down_lifts_body },
        { "push back moves forward", test_push_back_moves_forward },
        { "hand stays put while stuck", test_hand_stays_put_while_stuck },
        { "lift off releases without dragging", test_lift_off_releases_without_dragging },
        { "fast push launches", test_fast_push_launches },
        { "multiplier and cap", test_multiplier_and_cap },
        { "slow release does not launch", test_slow_release_does_not_launch },
        { "history length matters", test_history_length_matters },
        { "momentum carries into the next push", test_momentum_carries },
        { "head bob stays inside the body", test_head_bob_stays_inside },
        { "smack rises smoothly", test_smack_rises_smoothly },
        { "wall climb", test_wall_climb },
        { "body does not pass walls", test_body_does_not_pass_walls },
        { "room-scale walk collides", test_room_scale_walk_collides },
        { "slippery surface", test_slippery_surface },
        { "ice never grips", test_ice_never_grips },
        { "two hands average", test_two_hands_average },
        { "arm length clamp", test_arm_length_clamp },
        { "disabled hand does not grip", test_disabled_hand_does_not_grip },
        { "water paddle", test_water_paddle },
        { "still water is still", test_water_still_is_still },
        { "frozen mode", test_frozen_mode },
        { "reset teleports", test_reset_teleports },
        { "reset with the hands in the floor", test_reset_with_hands_in_the_floor },
        { "landing event", test_landing_event },
        { "zero dt is a no-op", test_zero_dt_is_noop },
        { "head height", test_head_height },
        { "external flight", test_external_flight },
    };
    unsigned i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = failures;
        tests[i].fn();
        printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
