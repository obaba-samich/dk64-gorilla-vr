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

/* MonkeSwim's TryUpdateAndClamp: add a push, but only while still short of the cap on the
 * push's own side; a speed already past it (a throw) is never cut back. */
static float add_clamped(float current, float delta, float max) {
    if (max > 0.0f) {
        if (max > current) {
            current = (current + delta < max) ? current + delta : max;
        }
    } else if (max < 0.0f) {
        if (max < current) {
            current = (current + delta > max) ? current + delta : max;
        }
    }
    return current;
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

    p->swim_accel = 1.17f;
    p->swim_resistance = 0.2f * 15.0f;
    p->swim_max_speed = 6.5f * 15.0f;
    p->paddle_look = 0.2f;
    p->swim_recovery = 0.3f;
    p->paddle_deadzone = 0.015f * 15.0f;
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
    if (vlen(w, off) >= p->unstick_distance) {
        return 0;
    }
    /* Still on something: pressed a little into its surface from where it's stuck, the hand
     * must still find it (as Gorilla Tag's hand cast always leans into the surface). A hand
     * caught inside a step's side, which the world lets out freely, finds nothing and lets go. */
    {
        gtl_hit hit;
        gtl_vec3 pressed = vsub(hand->pos, vmul(hand->normal, p->hand_radius * 0.25f));
        if (!w->sweep(w->user, hand->pos, pressed, p->hand_radius, &hit)) {
            return 0;
        }
    }
    return 1;
}

/* Where the controller says the hand is, in world space, held to arm's reach. */
static gtl_vec3 hand_target(const gtl_world *w, const gtl_params *p, const gtl_state *s,
                            const gtl_input *in, int h) {
    /* The head is where the tracked head is: head_height over the body at the play space's
     * origin height, plus how far the head has moved up or down from there (crouching, leaning
     * down). The body never follows the head up and down (see step 1), so crouching brings the
     * hands closer to the ground, as in Gorilla Tag. */
    gtl_vec3 rel = vsub(in->hand[h], in->head);
    rel = vclamp_len(w, rel, p->arm_length);
    return vadd(vadd(s->body, v3(0.0f, p->head_height + in->head.y, 0.0f)), rel);
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
        /* from the body's centre (free space): the first step sweeps out to the controller and
         * stops on whatever is in the way, instead of starting inside the ground (after a pause
         * with the hands low, both started stuck in it) */
        s->hand[h].pos = body_world;
        s->hand[h].touching = 0;
        s->hand[h].was_touching = 0;
        s->hand[h].slip = 0.0f;
        s->hand[h].surface = 0;
        s->hand[h].normal = v3(0, 1, 0);
        s->hand[h].last_ctrl = in ? in->hand[h] : v3(0, 0, 0);
        s->hand[h].stroking = 0;
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
    if (in->hand_hold[h]) {
        /* held by the grip: it stays where it closed, whatever is (or isn't) under it */
        if (!was) {
            hand->pos = target;
            hand->normal = v3(0, 1, 0);
            hand->slip = 0.0f;
            if (ev) {
                ev->hand_contact[h] = 1;
                ev->contact_speed[h] = 0.0f;
            }
        }
        hand->touching = 1;
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
    if (was && vlen(w, vsub(target, hand->pos)) >= p->unstick_distance) {
        /* let go by distance (Gorilla Tag: the hand jumps to the controller): look for the
         * surface again from the head, so it lands under the controller instead of grabbing the
         * old spot again in the same step and never pulling */
        from = vadd(s->body, v3(0.0f, p->head_height + in->head.y, 0.0f));
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
            s->hand[h].last_ctrl = in->hand[h];
            s->hand[h].stroking = 0;
        }
        return;
    }

    before = s->body;

    /* 1. room-scale head movement */
    head_delta = vsub(in->head, s->last_head);
    /* only walking around the room moves the body; the head's ups and downs stay inside it
     * (they used to lift the body off the floor for the game to drop it again: jitter) */
    head_delta.y = 0.0f;
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
            if (!in->hand_hold[h] && !still_stuck(w, p, hand, target)) {
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
        for (h = 0; h < 2; h++) {
            gtl_hand *hd = &s->hand[h];
            /* the hand's way back is the body's way forward */
            gtl_vec3 raw = vsub(hd->last_ctrl, in->hand[h]);
            float moved = vlen(w, raw);
            gtl_vec3 dir;
            float sp, dl;
            int stroke = in->hand_stroke[h];
            if (in->stroke_always && moved > GTL_EPS) {
                /* No button: the swimming motion itself. A hand pulling back, down or out
                 * (against where you look) is a stroke; a hand reaching forward is the recovery
                 * and pushes nothing, so breaststroke and crawl move you forward. */
                float ahead = vdot(raw, in->look) / moved;   /* raw is the hand's way back */
                stroke = (vlen(w, in->look) < GTL_EPS) || (ahead > -p->swim_recovery);
            }
            if (!in->hand_enabled[h] || hd->touching || !stroke || moved > p->arm_length) {
                hd->stroking = 0;   /* released (or a tracking jump): the next stroke starts fresh */
                continue;
            }
            if (!hd->stroking) {
                hd->stroking = 1;
                hd->stroke_sum = raw;
            } else {
                hd->stroke_sum = vadd(hd->stroke_sum, vmul(raw, 0.5f));
            }
            sp = moved / dt;
            dl = vlen(w, hd->stroke_sum);
            if (sp <= p->paddle_deadzone || dl < GTL_EPS) {
                continue;
            }
            dir = vadd(vmul(hd->stroke_sum, 1.0f / dl), vmul(in->look, p->paddle_look));
            dl = vlen(w, dir);
            if (dl < GTL_EPS) {
                continue;
            }
            dir = vmul(dir, 1.0f / dl);
            s->velocity.x = add_clamped(s->velocity.x, dir.x * p->swim_accel * sp * dt, dir.x * p->swim_max_speed);
            s->velocity.y = add_clamped(s->velocity.y, dir.y * p->swim_accel * sp * dt, dir.y * p->swim_max_speed);
            s->velocity.z = add_clamped(s->velocity.z, dir.z * p->swim_accel * sp * dt, dir.z * p->swim_max_speed);
        }
        s->velocity = vadd(s->velocity, v3(0, (p->buoyancy - p->gravity) * dt, 0));
        {
            /* the water's constant slow-down (Vector3.MoveTowards(v, 0, resistance x dt)) */
            float v = vlen(w, s->velocity);
            float slow = p->swim_resistance * dt;
            s->velocity = (v <= slow) ? v3(0, 0, 0) : vmul(s->velocity, (v - slow) / v);
        }
    }

    if (pulling > 0) {
        int ground = 0;
        gtl_vec3 moved_from = s->body;
        pull = vmul(pull, 1.0f / (float)pulling);
        /* A hand smacked into the ground overshoots it by up to a whole frame of hand travel;
         * pushing the body up by all of that at once is a pop (at 30 Hz a big one). Rising off a
         * surface goes no faster than a throw can, the rest follows over the next frames (the
         * hand stays pressed in, so it keeps pushing). */
        if (pull.y > p->max_jump_speed * dt) {
            pull.y = p->max_jump_speed * dt;
        }
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
        s->hand[h].last_ctrl = in->hand[h];
    }
    s->origin = vsub(s->body, in->head);
    (void)before;
}
