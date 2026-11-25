#include "gesture.h"

#define GST_PI 3.14159265f

/* --- maths without libc --- */

static float gst_abs(float x) { return x < 0.0f ? -x : x; }

static float gst_wrap(float a) {
    while (a > GST_PI) a -= 2.0f * GST_PI;
    while (a < -GST_PI) a += 2.0f * GST_PI;
    return a;
}

static float gst_sin(float a) {
    float a2;
    a = gst_wrap(a);
    if (a > 0.5f * GST_PI) a = GST_PI - a;
    else if (a < -0.5f * GST_PI) a = -GST_PI - a;
    a2 = a * a;
    return a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
}

static float gst_cos(float a) { return gst_sin(a + 0.5f * GST_PI); }

static float gst_atan2(float y, float x) {
    float ax = gst_abs(x), ay = gst_abs(y), a, s, r;
    if (ax < 1e-9f && ay < 1e-9f) return 0.0f;
    a = (ax < ay) ? ax / ay : ay / ax;
    s = a * a;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 0.5f * GST_PI - r;
    if (x < 0) r = GST_PI - r;
    if (y < 0) r = -r;
    return r;
}

/* --- setup --- */

void gst_default_params(gst_params *p) {
    p->dominant = 1;
    p->crouch_by_ducking = 1;
    p->duck_depth = 0.45f;
    p->slam_speed = 2.2f;
    p->punch_speed = 2.8f;
    p->throw_speed = 1.8f;
    p->twirl_width = 0.55f;
    p->climb_gain = 0.7f;
    p->pair_frames = 3;
}

void gst_reset(gst_state *s) {
    char *b = (char *)s;
    unsigned i;
    for (i = 0; i < sizeof(*s); i++) b[i] = 0;
}

void gst_local(const gst_input *in, const float v[3], int is_position, float out[3]) {
    float d[3];
    float sn = gst_sin(in->head_yaw), cs = gst_cos(in->head_yaw);
    d[0] = v[0] - (is_position ? in->head[0] : 0.0f);
    d[1] = v[1] - (is_position ? in->head[1] : 0.0f);
    d[2] = v[2] - (is_position ? in->head[2] : 0.0f);
    /* right = (cos, 0, -sin), forward = (-sin, 0, -cos) */
    out[0] = d[0] * cs - d[2] * sn;
    out[1] = d[1];
    out[2] = -d[0] * sn - d[2] * cs;
}

/* --- button sequences --- */

static void gst_play(gst_state *s, const gst_step *steps, int n) {
    int i;
    if (n > GST_SEQ_MAX) n = GST_SEQ_MAX;
    for (i = 0; i < n; i++) s->seq[i] = steps[i];
    s->seq_len = n;
    s->seq_pos = 0;
    s->seq_left = n > 0 ? steps[0].frames : 0;
}

static int gst_busy(const gst_state *s) { return s->seq_pos < s->seq_len; }

static unsigned gst_seq_mask(gst_state *s) {
    unsigned m;
    if (!gst_busy(s)) return 0;
    m = s->seq[s->seq_pos].mask;
    if (--s->seq_left <= 0) {
        s->seq_pos++;
        if (s->seq_pos < s->seq_len) s->seq_left = s->seq[s->seq_pos].frames;
    }
    return m;
}

static int gst_bit(unsigned ev) {
    int i = 0;
    while (ev > 1u) { ev >>= 1; i++; }
    return i;
}

static int gst_ready(const gst_state *s, unsigned ev) { return s->cooldown[gst_bit(ev)] <= 0.0f; }
static void gst_cool(gst_state *s, unsigned ev, float t) { s->cooldown[gst_bit(ev)] = t; }

/* Z held, then Z + the button, then let go: how the crouch handlers want a combo. */
static void gst_combo(gst_state *s, unsigned button) {
    gst_step st[3];
    st[0].mask = GST_N64_Z; st[0].frames = 2;
    st[1].mask = GST_N64_Z | button; st[1].frames = 2;
    st[2].mask = 0; st[2].frames = 1;
    gst_play(s, st, 3);
}

static void gst_tap(gst_state *s, unsigned button, int frames) {
    gst_step st[2];
    st[0].mask = button; st[0].frames = frames;
    st[1].mask = 0; st[1].frames = 1;
    gst_play(s, st, 2);
}

static void gst_aim(gst_state *s, float yaw, int frames) {
    s->aim_yaw = yaw;
    s->aim_frames = frames;
}

/* yaw of a play-space direction, same convention as head_yaw */
static float gst_yaw_of(float x, float z) { return gst_atan2(-x, -z); }

/* --- zones, in the head's local frame (x right, y up, z forward) --- */

static int gst_at_hip(const float l[3], int hand) {
    float side = hand == 0 ? -l[0] : l[0];
    return l[1] < -0.30f && l[1] > -0.85f && side > 0.10f && l[2] < 0.25f && l[2] > -0.30f;
}

static int gst_in_front_of_face(const float l[3]) {
    return l[1] > -0.35f && l[1] < 0.15f && l[2] > 0.10f && l[2] < 0.60f;
}

void gst_step_frame(gst_state *s, const gst_params *p, const gst_input *in, gst_output *out) {
    float lp[2][3], lv[2][3];
    int h, other, grip_down[2], grip_up[2], trig_down[2];
    unsigned hold = 0;
    int k;

    out->hold = 0;
    out->events = 0;
    out->stick_valid = 0;
    out->stick[0] = out->stick[1] = 0.0f;
    out->aim_valid = 0;
    out->aim_yaw = 0.0f;

    for (k = 0; k < 10; k++) {
        if (s->cooldown[k] > 0.0f) s->cooldown[k] -= in->dt;
    }
    for (h = 0; h < 2; h++) {
        gst_local(in, in->hand[h], 1, lp[h]);
        gst_local(in, in->hand_vel[h], 0, lv[h]);
        grip_down[h] = in->grip[h] > 0.6f && s->prev_grip[h] <= 0.6f;
        grip_up[h] = in->grip[h] <= 0.4f && s->prev_grip[h] > 0.4f;
        trig_down[h] = in->trigger[h] > 0.6f && s->prev_trigger[h] <= 0.6f;
    }

    /* standing height: follows the head up at once, comes down slowly (sitting, a new stance) */
    if (!s->standing_seen || in->head[1] > s->standing_y) {
        s->standing_y = in->head[1];
        s->standing_seen = 1;
    } else {
        s->standing_y -= 0.03f * in->dt;
    }

    if (!gst_busy(s)) {
        /* Simian Slam: both hands down hard while in the air */
        if (in->airborne && !in->in_water && gst_ready(s, GST_EV_SLAM)
            && lv[0][1] < -p->slam_speed && lv[1][1] < -p->slam_speed) {
            gst_tap(s, GST_N64_Z, 2);
            gst_cool(s, GST_EV_SLAM, 0.6f);
            out->events |= GST_EV_SLAM;
        }
    }

    /* Gun: dominant hand closes at the hip. Again to put it away. */
    h = p->dominant ? 1 : 0;
    other = 1 - h;
    if (!gst_busy(s) && !in->airborne && grip_down[h] && gst_at_hip(lp[h], h) && gst_ready(s, GST_EV_GUN)) {
        gst_combo(s, GST_N64_CL);
        gst_cool(s, GST_EV_GUN, 0.5f);
        out->events |= GST_EV_GUN;
    }

    /* Fire: the gun hand's trigger, aimed where that hand moves the shot */
    if (in->gun_out && trig_down[h] && !gst_busy(s)) {
        gst_tap(s, GST_N64_B, 2);
        out->events |= GST_EV_FIRE;
    }

    /* Orange: off hand closes at the hip, then a throw and let go */
    if (grip_down[other] && gst_at_hip(lp[other], other)) {
        s->orange_held = 1;
    }
    if (s->orange_held && grip_up[other]) {
        float sp2 = lv[other][0] * lv[other][0] + lv[other][1] * lv[other][1] + lv[other][2] * lv[other][2];
        s->orange_held = 0;
        if (sp2 > p->throw_speed * p->throw_speed && lv[other][2] > 0.0f && !gst_busy(s) && !in->airborne
            && gst_ready(s, GST_EV_ORANGE)) {
            gst_combo(s, GST_N64_CR);
            gst_aim(s, gst_yaw_of(in->hand_vel[other][0], in->hand_vel[other][2]), 6);
            gst_cool(s, GST_EV_ORANGE, 0.4f);
            out->events |= GST_EV_ORANGE;
        }
    }

    /* Instrument: both hands up in front of the face, both closed, held a moment */
    if (in->grip[0] > 0.6f && in->grip[1] > 0.6f && gst_in_front_of_face(lp[0]) && gst_in_front_of_face(lp[1])) {
        float dx = lp[0][0] - lp[1][0], dy = lp[0][1] - lp[1][1], dz = lp[0][2] - lp[1][2];
        if (dx * dx + dy * dy + dz * dz < 0.40f * 0.40f) {
            s->instrument_frames++;
        } else {
            s->instrument_frames = 0;
        }
    } else {
        s->instrument_frames = 0;
    }
    if (s->instrument_frames == 8 && !gst_busy(s) && !in->airborne && gst_ready(s, GST_EV_INSTRUMENT)) {
        gst_combo(s, GST_N64_CU);
        gst_cool(s, GST_EV_INSTRUMENT, 2.0f);
        out->events |= GST_EV_INSTRUMENT;
    }

    /* Punches: a fast forward jab from chest height, not holding on. Two together = special. */
    for (h = 0; h < 2; h++) {
        int jab = !in->touching[h] && in->grip[h] < 0.4f && lp[h][1] > -0.55f
            && lv[h][2] > p->punch_speed && lv[h][2] > 2.0f * gst_abs(lv[h][1]);
        if (jab && s->punch_wait[h] == 0 && gst_ready(s, GST_EV_PUNCH)) {
            s->punch_wait[h] = 1;
        }
    }
    if (s->punch_wait[0] && s->punch_wait[1]) {
        if (!gst_busy(s) && !in->gun_out) {
            gst_combo(s, GST_N64_B);
            gst_aim(s, in->head_yaw, 6);
            out->events |= GST_EV_SPECIAL;
        }
        gst_cool(s, GST_EV_PUNCH, 0.5f);
        s->punch_wait[0] = s->punch_wait[1] = 0;
    } else {
        for (h = 0; h < 2; h++) {
            if (s->punch_wait[h] && ++s->punch_wait[h] > p->pair_frames) {
                if (!gst_busy(s) && !in->gun_out) {
                    gst_tap(s, GST_N64_B, 2);
                    gst_aim(s, gst_yaw_of(in->hand_vel[h][0], in->hand_vel[h][2]), 4);
                    out->events |= GST_EV_PUNCH;
                }
                gst_cool(s, GST_EV_PUNCH, 0.35f);
                s->punch_wait[0] = s->punch_wait[1] = 0;
            }
        }
    }

    hold = gst_seq_mask(s);

    /* Holds that last as long as the pose does */
    if (in->airborne && !in->in_water && lp[0][0] < -p->twirl_width && lp[1][0] > p->twirl_width
        && lp[0][1] > -0.45f && lp[1][1] > -0.45f) {
        hold |= GST_N64_A;
        out->events |= GST_EV_TWIRL;
    }
    if (p->crouch_by_ducking && !in->airborne && !in->in_water
        && in->head[1] < s->standing_y - p->duck_depth) {
        hold |= GST_N64_Z;
        out->events |= GST_EV_CROUCH;
    }

    /* Climbing vines, ropes and ladders: a hand closed overhead, pulled down = up the vine */
    {
        float pull = 0.0f;
        int n = 0;
        for (h = 0; h < 2; h++) {
            if (in->grip[h] > 0.6f && lp[h][1] > -0.25f && !in->touching[h]) {
                pull += -lv[h][1];
                n++;
            }
        }
        if (n > 0) {
            float y = pull / (float)n * p->climb_gain;
            if (y > 1.0f) y = 1.0f;
            if (y < -1.0f) y = -1.0f;
            out->stick_valid = 1;
            out->stick[1] = y;
            out->events |= GST_EV_CLIMB;
        }
    }

    if (s->aim_frames > 0) {
        s->aim_frames--;
        out->aim_valid = 1;
        out->aim_yaw = s->aim_yaw;
    }
    if (in->gun_out) {
        /* the gun points where the gun hand points; hand_vel is no use while aiming still */
        out->aim_valid = 1;
        out->aim_yaw = s->aim_frames > 0 ? s->aim_yaw : in->head_yaw;
    }

    out->hold = hold;
    for (h = 0; h < 2; h++) {
        s->prev_grip[h] = in->grip[h];
        s->prev_trigger[h] = in->trigger[h];
    }
}
