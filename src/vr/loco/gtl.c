/*
 * Arm locomotion core. See gtl.h for the rules this file lives by (no libc, floats only).
 *
 * One step, in order:
 *   1. Room-scale: the tracked head moved, so move the body the same amount (with collision).
 *   2. Hands that were touching last step pull the body: body moves by (stuck hand - controller).
 *      With two hands touching we average them.
 *   3. If no hand holds us, fly: gravity (or water), then collide and slide.
 *   4. Place the hands. Each one sweeps from where it was drawn to where the controller is now and
 *      stops at the first surface, so the visible hand never goes through a wall.
 *   5. If every hand just let go, throw the body with the averaged hand-driven velocity.
 */
#include "gtl.h"

#define GTL_SKIN 0.01f
#define GTL_EPS 1e-6f

static gtl_vec3 v3(float x, float y, float z) { gtl_vec3 r; r.x = x; r.y = y; r.z = z; return r; }
static gtl_vec3 vadd(gtl_vec3 a, gtl_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static gtl_vec3 vsub(gtl_vec3 a, gtl_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static gtl_vec3 vmul(gtl_vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(gtl_vec3 a, gtl_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vlen(const gtl_world *w, gtl_vec3 a) { return w->sqrt_fn(vdot(a, a)); }
static float fclampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static gtl_vec3 vclamp_len(const gtl_world *w, gtl_vec3 a, float max_len) {
    float l = vlen(w, a);
    if (l > max_len && l > GTL_EPS) {
        return vmul(a, max_len / l);
    }
    return a;
}

void gtl_default_params(gtl_params *p) {
    /* Tuned for DK at the default world scale (15 units per metre). */
    p->body_radius = 4.0f;
    p->hand_radius = 1.2f;
    p->arm_length = 14.0f;
    p->head_height = 0.0f;

    p->jump_multiplier = 1.1f;
    p->max_jump_speed = 150.0f;
    p->min_launch_speed = 12.0f;
    p->history_frames = 6;

    p->gravity = 150.0f;
    p->terminal_velocity = 300.0f;
    p->ground_friction = 6.0f;
    p->ground_normal_y = 0.6f;

    p->unstick_distance = 3.0f;
    p->slip_threshold = 0.95f;

    p->paddle_strength = 0.08f;
    p->water_drag = 1.5f;
    p->buoyancy = 150.0f;

    p->max_slide_iterations = 4;
}

/* Move a sphere by delta, sliding along whatever it hits. Returns the end position. */
static gtl_vec3 slide(const gtl_world *w, const gtl_params *p, gtl_vec3 pos, gtl_vec3 delta,
                      float radius, int *hit_ground, gtl_vec3 *last_normal, int *hit_any) {
    int i;
    for (i = 0; i < p->max_slide_iterations; i++) {
        gtl_hit hit;
        float len = vlen(w, delta);
        float t;
        if (len < GTL_EPS) {
            break;
        }
        if (!w->sweep(w->user, pos, vadd(pos, delta), radius, &hit)) {
            pos = vadd(pos, delta);
            break;
        }
        /* stop a hair before the surface so the next sweep doesn't start inside it */
        t = hit.t - GTL_SKIN / len;
        if (t < 0.0f) {
            t = 0.0f;
        }
        pos = vadd(pos, vmul(delta, t));
        delta = vmul(delta, 1.0f - t);
        delta = vsub(delta, vmul(hit.normal, vdot(delta, hit.normal)));
        if (hit_any) {
            *hit_any = 1;
        }
        if (last_normal) {
            *last_normal = hit.normal;
        }
        if (hit_ground && hit.normal.y > p->ground_normal_y) {
            *hit_ground = 1;
        }
    }
    return pos;
}

static int history_len(const gtl_params *p) {
    int n = p->history_frames;
    if (n < 1) {
        n = 1;
    }
    if (n > GTL_HISTORY_MAX) {
        n = GTL_HISTORY_MAX;
    }
    return n;
}

static void history_push(gtl_state *s, const gtl_params *p, gtl_vec3 v) {
    int n = history_len(p);
    if (s->history_count > n) {
        /* history_frames got shorter: keep only the newest n samples */
        gtl_vec3 keep[GTL_HISTORY_MAX];
        int i, m = s->history_count;
        for (i = 0; i < n; i++) {
            keep[n - 1 - i] = s->history[(s->history_head - i + m) % m];
        }
        for (i = 0; i < n; i++) {
            s->history[i] = keep[i];
        }
        s->history_count = n;
        s->history_head = n - 1;
    }
    s->history_head = (s->history_head + 1) % n;
    s->history[s->history_head] = v;
    if (s->history_count < n) {
        s->history_count++;
    }
}

gtl_vec3 gtl_history_velocity(const gtl_state *s, const gtl_params *p) {
    gtl_vec3 sum = v3(0, 0, 0);
    int i;
    (void)p;
    if (s->history_count == 0) {
        return sum;
    }
    for (i = 0; i < s->history_count; i++) {
        sum = vadd(sum, s->history[i]);
    }
    return vmul(sum, 1.0f / (float)s->history_count);
}

/* Does a hand that was stuck at hand->pos stay stuck with the controller at target?
 * Pushing into the surface keeps it, pulling out of it or stretching too far lets go. */
static int still_stuck(const gtl_world *w, const gtl_params *p, const gtl_hand *hand,
                       gtl_vec3 target) {
    gtl_vec3 off = vsub(target, hand->pos);
    if (hand->slip >= p->slip_threshold) {
        return 0;
    }
    if (vdot(off, hand->normal) > p->hand_radius * 0.25f) {
        return 0;
    }
    return vlen(w, off) < p->unstick_distance;
}

/* Where the controller says the hand is, in world space, held to arm's reach. */
static gtl_vec3 hand_target(const gtl_world *w, const gtl_params *p, const gtl_state *s,
                            const gtl_input *in, int h) {
    gtl_vec3 rel = vsub(in->hand[h], in->head);
    rel = vclamp_len(w, rel, p->arm_length);
    return vadd(vadd(s->body, v3(0.0f, p->head_height, 0.0f)), rel);
}

void gtl_reset(gtl_state *s, gtl_vec3 body_world, const gtl_input *in) {
    int h;
    s->body = body_world;
    s->velocity = v3(0, 0, 0);
    s->grounded = 0;
    s->history_count = 0;
    s->history_head = 0;
    s->has_last_end = 0;
    s->last_head = in ? in->head : v3(0, 0, 0);
    s->origin = vsub(body_world, s->last_head);
    for (h = 0; h < 2; h++) {
        s->hand[h].pos = in ? vadd(body_world, vsub(in->hand[h], in->head)) : body_world;
        s->hand[h].touching = 0;
        s->hand[h].was_touching = 0;
        s->hand[h].slip = 0.0f;
        s->hand[h].surface = 0;
        s->hand[h].normal = v3(0, 1, 0);
        s->hand[h].last_rel = in ? vsub(in->hand[h], in->head) : v3(0, 0, 0);
    }
    s->initialised = 1;
}

static void place_hand(gtl_state *s, const gtl_params *p, const gtl_world *w, const gtl_input *in,
                       int h, gtl_events *ev, float dt) {
    gtl_hand *hand = &s->hand[h];
    gtl_vec3 target = hand_target(w, p, s, in, h);
    gtl_vec3 from = hand->pos;
    gtl_vec3 move;
    gtl_hit hit;
    int was = hand->touching;
    float speed;

    hand->was_touching = was;

    if (!in->hand_enabled[h]) {
        hand->pos = target;
        hand->touching = 0;
        return;
    }

    /* A stuck hand stays put until the controller pulls far enough away from it, or pulls
     * out of the surface. Pushing further in keeps it stuck. */
    if (was && still_stuck(w, p, hand, target)) {
        /* slippery surfaces let the hand drift along the surface towards the controller */
        if (hand->slip > 0.0f) {
            gtl_vec3 off = vsub(target, hand->pos);
            gtl_vec3 along = vsub(off, vmul(hand->normal, vdot(off, hand->normal)));
            hand->pos = vadd(hand->pos, vmul(along, hand->slip));
        }
        hand->touching = 1;
        return;
    }

    move = vsub(target, from);
    speed = dt > 0.0f ? vlen(w, move) / dt : 0.0f;
    if (w->sweep(w->user, from, target, p->hand_radius, &hit)) {
        float len = vlen(w, move);
        float t = len > GTL_EPS ? hit.t - GTL_SKIN / len : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        hand->pos = vadd(from, vmul(move, t));
        hand->normal = hit.normal;
        hand->slip = fclampf(hit.slip, 0.0f, 1.0f);
        hand->surface = hit.surface;
        hand->touching = hand->slip < p->slip_threshold;
        if (hand->touching && !was && ev) {
            ev->hand_contact[h] = 1;
            ev->contact_speed[h] = speed;
        }
    } else {
        hand->pos = target;
        hand->touching = 0;
    }
    if (was && !hand->touching && ev) {
        ev->hand_release[h] = 1;
    }
}

void gtl_step(gtl_state *s, const gtl_params *p, const gtl_world *w, const gtl_input *in,
              gtl_events *ev) {
    gtl_events local;
    gtl_vec3 head_delta;
    gtl_vec3 pull = v3(0, 0, 0);
    gtl_vec3 before;
    int pulling = 0;
    int was_holding;
    int holding;
    int was_grounded = s->grounded;
    int h;
    float dt = in->dt;

    if (!ev) {
        ev = &local;
    }
    for (h = 0; h < 2; h++) {
        ev->hand_contact[h] = 0;
        ev->contact_speed[h] = 0.0f;
        ev->hand_release[h] = 0;
    }
    ev->launched = 0;
    ev->launch_velocity = v3(0, 0, 0);
    ev->landed = 0;

    if (!s->initialised) {
        gtl_reset(s, v3(0, 0, 0), in);
    }
    if (dt <= 0.0f) {
        return;
    }

    if (in->mode == GTL_MODE_FROZEN) {
        /* the game owns the body; just keep the hands on the controllers */
        s->last_head = in->head;
        s->velocity = v3(0, 0, 0);
        s->history_count = 0;
        s->has_last_end = 0;
        for (h = 0; h < 2; h++) {
            s->hand[h].pos = hand_target(w, p, s, in, h);
            s->hand[h].touching = 0;
            s->hand[h].was_touching = 0;
            s->hand[h].last_rel = vsub(in->hand[h], in->head);
        }
        return;
    }

    before = s->body;

    /* 1. room-scale head movement */
    head_delta = vsub(in->head, s->last_head);
    s->last_head = in->head;
    s->body = slide(w, p, s->body, head_delta, p->body_radius, 0, 0, 0);

    /* 2. stuck hands push and pull the body */
    was_holding = 0;
    for (h = 0; h < 2; h++) {
        gtl_hand *hand = &s->hand[h];
        if (hand->touching && in->hand_enabled[h]) {
            gtl_vec3 target = hand_target(w, p, s, in, h);
            gtl_vec3 d;
            was_holding = 1;
            if (!still_stuck(w, p, hand, target)) {
                /* lifting off: let go now instead of dragging the body towards the hand */
                continue;
            }
            d = vsub(hand->pos, target);
            d = vmul(d, 1.0f - hand->slip);
            pull = vadd(pull, d);
            pulling++;
        }
    }

    if (in->mode == GTL_MODE_WATER) {
        /* paddling: moving a hand through water pushes you the other way. Drag goes with speed
         * squared, so a quick stroke back and a slow reach forward nets forward motion. */
        for (h = 0; h < 2; h++) {
            if (in->hand_enabled[h] && !s->hand[h].touching) {
                gtl_vec3 rel = vsub(in->hand[h], in->head);
                gtl_vec3 hv = vmul(vsub(rel, s->hand[h].last_rel), 1.0f / dt);
                float sp = vlen(w, hv);
                s->velocity = vsub(s->velocity, vmul(hv, p->paddle_strength * sp * dt));
            }
        }
        s->velocity = vclamp_len(w, s->velocity, p->max_jump_speed);
        s->velocity = vadd(s->velocity, v3(0, (p->buoyancy - p->gravity) * dt, 0));
        s->velocity = vmul(s->velocity, 1.0f / (1.0f + p->water_drag * dt));
    }

    if (pulling > 0) {
        int ground = 0;
        gtl_vec3 moved_from = s->body;
        pull = vmul(pull, 1.0f / (float)pulling);
        s->body = slide(w, p, s->body, pull, p->body_radius, &ground, 0, 0);
        s->velocity = v3(0, 0, 0);
        (void)moved_from;
        s->grounded = ground;
    } else if (in->external_flight && in->mode != GTL_MODE_WATER) {
        /* the game moves the body; the caller syncs s->body from it before the next step */
        s->velocity = v3(0, 0, 0);
    } else {
        /* 3. free flight */
        int ground = 0;
        int hit_any = 0;
        gtl_vec3 n = v3(0, 1, 0);
        gtl_vec3 prev;
        if (in->mode != GTL_MODE_WATER) {
            s->velocity.y -= p->gravity * dt;
            if (s->velocity.y < -p->terminal_velocity) {
                s->velocity.y = -p->terminal_velocity;
            }
        }
        prev = s->body;
        s->body = slide(w, p, s->body, vmul(s->velocity, dt), p->body_radius, &ground, &n, &hit_any);
        if (hit_any) {
            /* lose the speed going into whatever we hit */
            float into = vdot(s->velocity, n);
            if (into < 0.0f) {
                s->velocity = vsub(s->velocity, vmul(n, into));
            }
        }
        if (!ground && in->mode != GTL_MODE_WATER) {
            /* resting on the floor: probe a little down so we stay grounded while standing still */
            gtl_hit probe;
            gtl_vec3 down = vadd(s->body, v3(0, -GTL_SKIN * 4.0f, 0));
            if (w->sweep(w->user, s->body, down, p->body_radius, &probe)
                && probe.normal.y > p->ground_normal_y) {
                ground = 1;
            }
        }
        s->grounded = ground;
        if (ground) {
            float k = 1.0f / (1.0f + p->ground_friction * dt);
            if (s->velocity.y < 0.0f) {
                s->velocity.y = 0.0f;
            }
            s->velocity.x *= k;
            s->velocity.z *= k;
        }
        (void)prev;
    }

    /* 4. hands */
    for (h = 0; h < 2; h++) {
        place_hand(s, p, w, in, h, ev, dt);
    }

    /* 5. launch when the last hand lets go */
    holding = 0;
    for (h = 0; h < 2; h++) {
        if (s->hand[h].touching && in->hand_enabled[h]) {
            holding = 1;
        }
    }
    if (was_holding && !holding) {
        gtl_vec3 v = gtl_history_velocity(s, p);
        float speed = vlen(w, v);
        if (speed > p->min_launch_speed) {
            v = vmul(v, p->jump_multiplier);
            v = vclamp_len(w, v, p->max_jump_speed);
            s->velocity = v;
            ev->launched = 1;
            ev->launch_velocity = v;
        }
    }

    /* body velocity this step, from wherever the last step left it (so a game that moved
     * the body in between, or pushed it back off a wall, counts too). After the launch check,
     * so a release uses the steps up to the last one that held on. */
    history_push(s, p, vmul(vsub(s->body, s->has_last_end ? s->last_end : before), 1.0f / dt));
    s->last_end = s->body;
    s->has_last_end = 1;

    if (s->grounded && !was_grounded) {
        ev->landed = 1;
    }
    for (h = 0; h < 2; h++) {
        s->hand[h].last_rel = vsub(in->hand[h], in->head);
    }
    s->origin = vsub(s->body, in->head);
    (void)before;
}
