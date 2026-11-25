#include <stdio.h>
#include <string.h>
#include "../gesture.h"

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

#define DT (1.0f / 30.0f)

typedef struct rig {
    gst_state s;
    gst_params p;
    gst_input in;
    gst_output out;
    unsigned seen_hold;     /* OR of everything held */
    unsigned events;        /* OR of events */
    unsigned first_hold[32];
    int frames;
} rig;

/* standing, hands relaxed at the sides (not at the hips' grab zone: further out and lower) */
static void rig_init(rig *r) {
    memset(r, 0, sizeof(*r));
    gst_default_params(&r->p);
    gst_reset(&r->s);
    r->in.head[1] = 0.0f;
    r->in.hand[0][0] = -0.25f; r->in.hand[0][1] = -0.95f; r->in.hand[0][2] = 0.0f;
    r->in.hand[1][0] = 0.25f; r->in.hand[1][1] = -0.95f; r->in.hand[1][2] = 0.0f;
    r->in.dt = DT;
}

static void rig_run(rig *r, int n) {
    int i;
    for (i = 0; i < n; i++) {
        gst_step_frame(&r->s, &r->p, &r->in, &r->out);
        if (r->frames < 32) r->first_hold[r->frames] = r->out.hold;
        r->frames++;
        r->seen_hold |= r->out.hold;
        r->events |= r->out.events;
    }
}

static void set_hand(rig *r, int h, float x, float y, float z) {
    r->in.hand[h][0] = x; r->in.hand[h][1] = y; r->in.hand[h][2] = z;
}

static void set_vel(rig *r, int h, float x, float y, float z) {
    r->in.hand_vel[h][0] = x; r->in.hand_vel[h][1] = y; r->in.hand_vel[h][2] = z;
}

static void test_idle_presses_nothing(void) {
    rig r;
    rig_init(&r);
    rig_run(&r, 60);
    CHECK(r.seen_hold == 0, "held %x", r.seen_hold);
    CHECK(r.events == 0, "events %x", r.events);
}

static void test_slam_in_air(void) {
    rig r;
    rig_init(&r);
    r.in.airborne = 1;
    set_vel(&r, 0, 0, -3.0f, 0);
    set_vel(&r, 1, 0, -3.0f, 0);
    rig_run(&r, 1);
    CHECK(r.events & GST_EV_SLAM, "no slam event");
    CHECK(r.first_hold[0] == GST_N64_Z, "first frame %x", r.first_hold[0]);
    rig_run(&r, 5);
    CHECK((r.seen_hold & ~GST_N64_Z) == 0, "only Z, got %x", r.seen_hold);
}

static void test_slam_needs_air_and_both_hands(void) {
    rig r;
    rig_init(&r);
    set_vel(&r, 0, 0, -3.0f, 0);
    set_vel(&r, 1, 0, -3.0f, 0);
    rig_run(&r, 3);
    CHECK(!(r.events & GST_EV_SLAM), "slammed on the ground");
    rig_init(&r);
    r.in.airborne = 1;
    set_vel(&r, 0, 0, -3.0f, 0);
    rig_run(&r, 3);
    CHECK(!(r.events & GST_EV_SLAM), "slammed with one hand");
}

static void test_punch(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 1, 0.2f, -0.3f, 0.3f);
    set_vel(&r, 1, 0, 0, 0);
    rig_run(&r, 1);
    /* forward is -z in play space at yaw 0 */
    set_vel(&r, 1, 0.0f, 0.2f, -3.5f);
    rig_run(&r, 1);
    set_vel(&r, 1, 0, 0, 0);
    rig_run(&r, 8);
    CHECK(r.events & GST_EV_PUNCH, "no punch");
    CHECK(!(r.events & GST_EV_SPECIAL), "single jab became a special");
    CHECK(r.seen_hold == GST_N64_B, "held %x", r.seen_hold);
}

static void test_punch_follows_head_yaw(void) {
    rig r;
    rig_init(&r);
    r.in.head_yaw = 1.5707963f;  /* turned left: forward is -x */
    set_hand(&r, 1, -0.3f, -0.3f, -0.2f);
    set_vel(&r, 1, -3.5f, 0.0f, 0.0f);
    rig_run(&r, 1);
    set_vel(&r, 1, 0, 0, 0);
    rig_run(&r, 8);
    CHECK(r.events & GST_EV_PUNCH, "no punch when facing left");
}

static void test_rowing_is_not_a_punch(void) {
    rig r;
    rig_init(&r);
    /* reaching forward low to plant a hand, and pulling back while touching */
    set_hand(&r, 1, 0.2f, -0.9f, -0.3f);
    set_vel(&r, 1, 0.0f, -1.5f, -3.2f);
    rig_run(&r, 2);
    r.in.touching[1] = 1;
    set_vel(&r, 1, 0.0f, 0.0f, 3.0f);
    rig_run(&r, 10);
    CHECK(r.seen_hold == 0, "rowing pressed %x", r.seen_hold);
}

static void test_double_punch_is_special(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 0, -0.2f, -0.3f, -0.3f);
    set_hand(&r, 1, 0.2f, -0.3f, -0.3f);
    set_vel(&r, 0, 0, 0, -3.5f);
    rig_run(&r, 1);
    set_vel(&r, 1, 0, 0, -3.5f);
    rig_run(&r, 1);
    set_vel(&r, 0, 0, 0, 0);
    set_vel(&r, 1, 0, 0, 0);
    rig_run(&r, 8);
    CHECK(r.events & GST_EV_SPECIAL, "no special");
    CHECK(!(r.events & GST_EV_PUNCH), "special also punched");
    CHECK((r.seen_hold & (GST_N64_Z | GST_N64_B)) == (GST_N64_Z | GST_N64_B), "held %x", r.seen_hold);
}

static void test_gun_draw_at_hip(void) {
    rig r;
    int i, z_first = -1, cl_first = -1;
    rig_init(&r);
    set_hand(&r, 1, 0.22f, -0.6f, 0.0f);
    rig_run(&r, 1);
    r.in.grip[1] = 1.0f;
    rig_run(&r, 8);
    CHECK(r.events & GST_EV_GUN, "no gun");
    for (i = 0; i < r.frames && i < 32; i++) {
        if ((r.first_hold[i] & GST_N64_Z) && z_first < 0) z_first = i;
        if ((r.first_hold[i] & GST_N64_CL) && cl_first < 0) cl_first = i;
    }
    CHECK(z_first >= 0 && cl_first > z_first, "Z at %d, C-left at %d", z_first, cl_first);
    CHECK(r.first_hold[cl_first] & GST_N64_Z, "C-left without Z held");
}

static void test_grip_away_from_hip_is_not_gun(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 1, 0.3f, -0.1f, 0.4f);
    rig_run(&r, 1);
    r.in.grip[1] = 1.0f;
    rig_run(&r, 8);
    CHECK(!(r.events & GST_EV_GUN), "gun from a grab in front");
}

static void test_fire_with_trigger(void) {
    rig r;
    rig_init(&r);
    r.in.gun_out = 1;
    rig_run(&r, 1);
    r.in.trigger[1] = 1.0f;
    rig_run(&r, 4);
    CHECK(r.events & GST_EV_FIRE, "no fire");
    CHECK(r.seen_hold == GST_N64_B, "held %x", r.seen_hold);
    CHECK(r.out.aim_valid, "gun out should aim");
}

static void test_orange_throw(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 0, -0.22f, -0.6f, 0.0f);
    rig_run(&r, 1);
    r.in.grip[0] = 1.0f;
    rig_run(&r, 3);
    CHECK(!(r.events & GST_EV_ORANGE), "orange before the throw");
    set_hand(&r, 0, -0.2f, -0.2f, -0.4f);
    set_vel(&r, 0, 0.0f, 0.5f, -3.0f);
    r.in.grip[0] = 0.0f;
    rig_run(&r, 1);
    CHECK(r.events & GST_EV_ORANGE, "no orange");
    CHECK(r.out.aim_valid, "orange should aim");
    CHECK(r.out.aim_yaw > -0.2f && r.out.aim_yaw < 0.2f, "aim %f", r.out.aim_yaw);
    rig_run(&r, 6);
    CHECK((r.seen_hold & (GST_N64_Z | GST_N64_CR)) == (GST_N64_Z | GST_N64_CR), "held %x", r.seen_hold);
}

static void test_orange_released_after_the_swing(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 0, -0.22f, -0.6f, 0.0f);
    rig_run(&r, 1);
    r.in.grip[0] = 1.0f;
    rig_run(&r, 3);
    set_hand(&r, 0, -0.2f, -0.3f, -0.3f);
    set_vel(&r, 0, 0.0f, 2.0f, -3.5f);
    rig_run(&r, 2);
    CHECK(!(r.events & GST_EV_CLIMB), "the throwing hand climbed");
    set_hand(&r, 0, -0.2f, -0.2f, -0.6f);
    set_vel(&r, 0, 0, 0, 0);
    rig_run(&r, 1);
    r.in.grip[0] = 0.0f;
    rig_run(&r, 1);
    CHECK(r.events & GST_EV_ORANGE, "late release lost the orange");
}

static void test_punch_into_a_wall(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 1, 0.2f, -0.3f, -0.3f);
    r.in.touching[1] = 1;
    set_vel(&r, 1, 0.0f, 0.0f, -3.5f);
    rig_run(&r, 1);
    set_vel(&r, 1, 0, 0, 0);
    rig_run(&r, 8);
    CHECK(r.events & GST_EV_PUNCH, "no punch against a wall");
}

static void test_orange_dropped_slowly_is_nothing(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 0, -0.22f, -0.6f, 0.0f);
    rig_run(&r, 1);
    r.in.grip[0] = 1.0f;
    rig_run(&r, 3);
    r.in.grip[0] = 0.0f;
    rig_run(&r, 5);
    CHECK(!(r.events & GST_EV_ORANGE), "orange without a throw");
}

static void test_instrument(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 0, -0.08f, -0.15f, -0.3f);
    set_hand(&r, 1, 0.08f, -0.15f, -0.3f);
    r.in.grip[0] = r.in.grip[1] = 1.0f;
    rig_run(&r, 5);
    CHECK(!(r.events & GST_EV_INSTRUMENT), "too soon");
    rig_run(&r, 10);
    CHECK(r.events & GST_EV_INSTRUMENT, "no instrument");
    CHECK((r.seen_hold & (GST_N64_Z | GST_N64_CU)) == (GST_N64_Z | GST_N64_CU), "held %x", r.seen_hold);
    /* note: the two grips overhead also count as climbing, which only matters on a vine */
}

static void test_twirl_in_air(void) {
    rig r;
    rig_init(&r);
    r.in.airborne = 1;
    set_hand(&r, 0, -0.7f, -0.2f, 0.0f);
    set_hand(&r, 1, 0.7f, -0.2f, 0.0f);
    rig_run(&r, 3);
    CHECK(r.out.hold & GST_N64_A, "no A hold");
    r.in.airborne = 0;
    rig_run(&r, 1);
    CHECK(!(r.out.hold & GST_N64_A), "A held on the ground");
}

static void test_duck_crouch(void) {
    rig r;
    rig_init(&r);
    rig_run(&r, 10);
    r.in.head[1] = -0.5f;
    rig_run(&r, 1);
    CHECK(r.out.hold & GST_N64_Z, "no crouch when ducking");
    r.in.head[1] = -0.1f;
    rig_run(&r, 1);
    CHECK(!(r.out.hold & GST_N64_Z), "crouch while standing");
    r.in.head[1] = -0.6f;
    r.in.touching[0] = 1;
    rig_run(&r, 1);
    CHECK(!(r.out.hold & GST_N64_Z), "crouch while a hand is on the ground (rowing low)");
    r.in.touching[0] = 0;
    r.p.crouch_by_ducking = 0;
    r.in.head[1] = -0.6f;
    rig_run(&r, 1);
    CHECK(!(r.out.hold & GST_N64_Z), "crouch when turned off");
}

static void test_climb_pull(void) {
    rig r;
    rig_init(&r);
    set_hand(&r, 1, 0.1f, 0.1f, -0.2f);
    r.in.grip[1] = 1.0f;
    set_vel(&r, 1, 0.0f, -1.0f, 0.0f);
    rig_run(&r, 1);
    CHECK(r.out.stick_valid && r.out.stick[1] > 0.5f, "pull down should climb up, %d %f", r.out.stick_valid,
        r.out.stick[1]);
    set_vel(&r, 1, 0.0f, 2.0f, 0.0f);
    rig_run(&r, 1);
    CHECK(r.out.stick[1] < -0.9f, "push up should climb down, %f", r.out.stick[1]);
    r.in.grip[1] = 0.0f;
    rig_run(&r, 1);
    CHECK(!r.out.stick_valid, "stick without a grip");
}

static void test_left_handed(void) {
    rig r;
    rig_init(&r);
    r.p.dominant = 0;
    set_hand(&r, 0, -0.22f, -0.6f, 0.0f);
    rig_run(&r, 1);
    r.in.grip[0] = 1.0f;
    rig_run(&r, 8);
    CHECK(r.events & GST_EV_GUN, "left-handed gun");
}

int main(void) {
    test_idle_presses_nothing();
    test_slam_in_air();
    test_slam_needs_air_and_both_hands();
    test_punch();
    test_punch_follows_head_yaw();
    test_rowing_is_not_a_punch();
    test_double_punch_is_special();
    test_gun_draw_at_hip();
    test_grip_away_from_hip_is_not_gun();
    test_fire_with_trigger();
    test_orange_throw();
    test_orange_dropped_slowly_is_nothing();
    test_orange_released_after_the_swing();
    test_punch_into_a_wall();
    test_instrument();
    test_twirl_in_air();
    test_duck_crouch();
    test_climb_pull();
    test_left_handed();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
