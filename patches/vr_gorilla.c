// Arm locomotion in the game.
//
// Each game frame, before the actors update:
//   1. read the controllers from the host (metres, play space),
//   2. turn them into world units around the body, with the body's heading,
//   3. run the locomotion core (src/vr/loco/gtl.c) against DK64's own floors,
//   4. leave the resulting displacement for the player's movement step to apply.
// The player's movement step (func_global_asm_806651FC, patched below) applies that displacement
// in place of the game's own while a hand holds on, so DK64's wall and floor passes, loading
// zones and triggers all see an ordinary move. With no hand holding, the game's own gravity,
// jumping, slopes and landing run as usual. A release asks the host to press A for a jump and
// then swaps the jump's speed for the throw's.
//
// After the frame we publish where the head and hands ended up, for the camera and hand drawing.
#include "common_structs.h"
#include "vr_state.h"
#include "vr_gorilla.h"
#include "../src/vr/loco/gtl.h"
#include "../src/vr/loco/gesture.h"

extern u8 is_cutscene_active;
extern u8 cc_number_of_players;
extern u8 current_character_index[];

// floor under x,z below y (and the surface/water it found, read right after)
u8 func_global_asm_8066635C(Actor *arg0, f32 x, f32 y, f32 z, f32 *arg4);
u8 func_global_asm_80666AE0(void);
u8 func_global_asm_8066715C(f32 *arg0);
// sin and cos of a 0..4095 heading
f32 func_global_asm_80612794(s16 arg0);
f32 func_global_asm_80612790(s16 arg0);

s32 vr_current_scene(void);
extern Maps current_map;

// Maps where the player rides something the arms can't drive: the third-person VR camera and
// the stick take over (docs/DECISIONS.md).
static s32 vr_vehicle_map(void) {
    switch (current_map) {
        case MAP_JAPES_MINECART:
        case MAP_FUNGI_MINECART:
        case MAP_CASTLE_MINECART:
        case MAP_MINECART_MAYHEM_EASY:
        case MAP_MINECART_MAYHEM_NORMAL:
        case MAP_MINECART_MAYHEM_HARD:
        case MAP_AZTEC_BEETLE_RACE:
        case MAP_CAVES_BEETLE_RACE:
        case MAP_FACTORY_CAR_RACE:
        case MAP_CASTLE_CAR_RACE:
        case MAP_GALLEON_SEAL_RACE:
        case MAP_GALLEON_PUFFTOSS:
        case MAP_KROOL_FIGHT_DIDDY_PHASE:
            return 1;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------------------------
// small maths, no libc here

#define VR_PI 3.14159265f
#define VR_TWO_PI 6.28318531f

static f32 vr_sqrt(f32 x) {
    return x > 0.0f ? __builtin_sqrtf(x) : 0.0f;
}

static f32 vr_wrap(f32 a) {
    while (a > VR_PI) {
        a -= VR_TWO_PI;
    }
    while (a < -VR_PI) {
        a += VR_TWO_PI;
    }
    return a;
}

// Good to about 1e-4, plenty for headings.
static f32 vr_sin(f32 a) {
    f32 a2;
    a = vr_wrap(a);
    if (a > 0.5f * VR_PI) {
        a = VR_PI - a;
    } else if (a < -0.5f * VR_PI) {
        a = -VR_PI - a;
    }
    a2 = a * a;
    return a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
}

static f32 vr_cos(f32 a) {
    return vr_sin(a + 0.5f * VR_PI);
}

static f32 vr_atan2(f32 y, f32 x) {
    f32 ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a, s, r;
    if (ax < 1e-9f && ay < 1e-9f) {
        return 0.0f;
    }
    a = (ax < ay) ? ax / ay : ay / ax;
    s = a * a;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = 0.5f * VR_PI - r;
    }
    if (x < 0) {
        r = VR_PI - r;
    }
    if (y < 0) {
        r = -r;
    }
    return r;
}

// Game headings: 0..4095 is a full turn, forward is (sin, cos) in x/z.
static s16 vr_heading_to_game(f32 yaw) {
    s32 h = (s32)(vr_wrap(yaw) * (4096.0f / VR_TWO_PI));
    return (s16)(h & 0xFFF);
}

static f32 vr_game_to_heading(s16 h) {
    return (f32)(h & 0xFFF) * (VR_TWO_PI / 4096.0f);
}

// ---------------------------------------------------------------------------------------------
// DK64's collision as a sphere sweep for the locomotion core.
//
// The game answers "what is the highest floor under x,z below height y" (the same query the
// camera and actors use), and reports the floor's surface and the water level after it. Floors
// cover the ground, slopes, platform tops, ledges and roofs. From that:
//   - a sphere whose bottom dips under the floor below it is touching that floor;
//   - a sphere that moves sideways into a spot whose floor top sits above its bottom (but not
//     too far above, or it is an overhang) has run into the side of a step, ledge or box: a wall.
// Flat walls with nothing on top (room walls) are left to the game's own wall pass for the body.

typedef struct VrSweepCtx {
    Actor *player;
    f32 wall_window;   // how far above a sphere a floor top can be and still count as a wall
} VrSweepCtx;

static VrSweepCtx vr_sweep_ctx;
static u8 vr_last_surface;

static s32 vr_floor_at(Actor *player, f32 x, f32 z, f32 from_y, f32 *out) {
    f32 y = from_y;
    u8 found = func_global_asm_8066635C(player, x, from_y, z, &y);
    vr_last_surface = func_global_asm_80666AE0();
    if (!found || y > from_y) {
        return 0;
    }
    *out = y;
    return 1;
}

// Surface 0xC is the game's slippery floor (ice and slides: it gets the slide sound and keeps
// a slide going above 40 speed; decomp playerStateEnterSlideBrake, playerUpdateSlopeSlidePhysics).
// Hands slide on it; everything else grips fully.
#define VR_SURFACE_SLIPPERY 0xC

static f32 vr_surface_slip(u8 surface) {
    return surface == VR_SURFACE_SLIPPERY ? 0.85f : 0.0f;
}

// Is a sphere of radius r at p touching geometry? If so, fill the normal.
static s32 vr_touching(VrSweepCtx *c, gtl_vec3 p, gtl_vec3 came_from, f32 r, gtl_hit *hit) {
    f32 floor;
    f32 top = p.y + r + c->wall_window;
    if (!vr_floor_at(c->player, p.x, p.z, top, &floor)) {
        return 0;
    }
    if (floor <= p.y - r) {
        return 0;
    }
    hit->surface = vr_last_surface;
    hit->slip = vr_surface_slip(vr_last_surface);
    if (floor > p.y - r + r * 1.5f) {
        // The floor top here is well above the sphere's bottom: we're in a step's side. Its
        // outward normal points to where the floor drops away: look around at the sphere's
        // radius. (Taking it from the way we came trapped hands: every move read as "in".)
        static const f32 dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
            { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f } };
        f32 nx = 0.0f, nz = 0.0f, l, f2;
        s32 k;
        for (k = 0; k < 8; k++) {
            f32 qx = p.x + dirs[k][0] * r * 1.5f, qz = p.z + dirs[k][1] * r * 1.5f;
            if (!vr_floor_at(c->player, qx, qz, top, &f2) || f2 <= p.y - r * 0.5f) {
                nx += dirs[k][0];
                nz += dirs[k][1];
            }
        }
        l = vr_sqrt(nx * nx + nz * nz);
        if (l < 1e-3f) {
            // deep inside: fall back to the way we came
            nx = came_from.x - p.x;
            nz = came_from.z - p.z;
            l = vr_sqrt(nx * nx + nz * nz);
            if (l < 1e-4f) {
                return 0;
            }
        }
        hit->normal.x = nx / l;
        hit->normal.y = 0.0f;
        hit->normal.z = nz / l;
    } else {
        // a floor: slope normal from two neighbouring samples
        f32 e = r * 0.5f, fx, fz;
        f32 nx = 0.0f, nz = 0.0f, l;
        if (vr_floor_at(c->player, p.x + e, p.z, top, &fx)) {
            nx = -(fx - floor) / e;
        }
        if (vr_floor_at(c->player, p.x, p.z + e, top, &fz)) {
            nz = -(fz - floor) / e;
        }
        l = vr_sqrt(nx * nx + 1.0f + nz * nz);
        hit->normal.x = nx / l;
        hit->normal.y = 1.0f / l;
        hit->normal.z = nz / l;
    }
    return 1;
}

static int vr_sweep(void *user, gtl_vec3 a, gtl_vec3 b, float r, gtl_hit *hit) {
    VrSweepCtx *c = (VrSweepCtx *)user;
    gtl_vec3 d, p, prev;
    f32 len, lo, hi;
    s32 steps, i, k;

    d.x = b.x - a.x; d.y = b.y - a.y; d.z = b.z - a.z;
    len = vr_sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    steps = (s32)(len / (r * 0.75f)) + 1;
    if (steps > 12) {
        steps = 12;
    }
    {
        // Starting inside the side of a step or ledge (floor top above us here): a side's
        // normal is taken from the way we came, so any move would read as "further in" and a
        // hand could never let go. Let whatever starts in there move out freely.
        f32 floor;
        if (vr_floor_at(c->player, a.x, a.z, a.y + r + c->wall_window, &floor) && floor > a.y - r + r * 1.5f) {
            return 0;
        }
    }
    prev = a;
    for (i = 1; i <= steps; i++) {
        f32 t = (f32)i / (f32)steps;
        p.x = a.x + d.x * t; p.y = a.y + d.y * t; p.z = a.z + d.z * t;
        if (vr_touching(c, p, prev, r, hit)) {
            // moving away from what we are already in is not a hit
            if (i == 1 && (d.x * hit->normal.x + d.y * hit->normal.y + d.z * hit->normal.z) >= 0.0f) {
                return 0;
            }
            // narrow down where along the segment we first touch
            lo = (f32)(i - 1) / (f32)steps;
            hi = t;
            for (k = 0; k < 4; k++) {
                gtl_hit tmp;
                f32 m = 0.5f * (lo + hi);
                gtl_vec3 q;
                q.x = a.x + d.x * m; q.y = a.y + d.y * m; q.z = a.z + d.z * m;
                if (vr_touching(c, q, prev, r, &tmp)) {
                    hi = m;
                } else {
                    lo = m;
                }
            }
            hit->t = lo;
            return 1;
        }
        prev = p;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// state

static gtl_state vr_loco;
static gtl_params vr_params;
static gtl_world vr_world;
static VrHands vr_hands;
static VrBody vr_body;
static s32 vr_active;              // arm locomotion drove the player this frame
static s32 vr_hide_body = 1;       // first person: don't draw the player's own model
static s32 vr_in_water;
static f32 vr_yaw;                 // body heading (radians, game convention)
static f32 vr_height;              // eyes above the feet at the recentre pose, world units
static f32 vr_ws = 26.0f;          // game units per metre this frame (see vr_setup_params)
static s32 vr_initialised;
static Actor *vr_player_seen;
static u32 vr_frame;

// For the movement patch: what to add to the player this frame.
static s32 vr_drive;               // 1 = replace the game's move with vr_disp
static gtl_vec3 vr_disp;

// A throw the arms own: after letting go, the locomotion core flies the body itself (momentum,
// gravity, sliding, hitting walls) instead of handing the game a jump or a run speed it would
// brake away. It ends on landing and slowing down, a hand grabbing something, water, or the
// game taking the player over (a hit, a slam, a vine, a barrel...).
#define VR_STATE_NORMAL 0x0C   // player states, see the note above vr_in_tag_barrel
#define VR_STATE_AIR 0x1E      // off the ground (seen falling, and while the hands hold the body up)
static s32 vr_launch_count;        // bumps on upward throws: the host presses A for the jump
static s32 vr_flying;
static s32 vr_fly_frames;
static u8 vr_fly_states[8];        // player states that belong to this throw
static s32 vr_fly_state_count;
static gtl_vec3 vr_fly_expect;     // where the throw put the body, to see the game push it back
#define VR_FLY_GRAVITY 9.8f        // m/s^2 at the world scale (Gorilla Tag uses Earth's)
#define VR_FLY_STOP 0.35f          // m/s: on the ground and slower than this, the game takes over

static void vr_fly_end(const char *why) {
    if (vr_flying) {
        recomp_printf("[vr-loco] throw over (%s) after %d frames\n", why, vr_fly_frames);
    }
    vr_flying = 0;
}

static s32 vr_fly_state_ok(u8 st) {
    s32 i;
    if (st == VR_STATE_NORMAL || st == VR_STATE_AIR) {
        return 1;
    }
    for (i = 0; i < vr_fly_state_count; i++) {
        if (vr_fly_states[i] == st) {
            return 1;
        }
    }
    if (vr_fly_frames <= 8 && vr_fly_state_count < 8) {
        // the jump the host's A press starts, and its first air states
        vr_fly_states[vr_fly_state_count++] = st;
        return 1;
    }
    return 0;
}

// Gestures (src/vr/loco/gesture.c, compiled in through vr_loco_core.c).
static gst_state vr_gst;
static gst_params vr_gst_params;
static gst_output vr_gst_out;
static s32 vr_tag_buzz;            // buzz both hands this frame (a gesture or a Kong swap)

// Raw fields the recomp's structs only name as unknowns (offsets from the DK64 decompilation's
// include/actor.h): actor contact flags (bit 0 on the ground, bit 1 in water) and the player
// state's gun-drawn byte.
#define VR_ACTOR_CONTACT(a) (*(u16 *)((u8 *)(a) + 0x6A))
#define VR_ACTOR_CUSTOM(a) (*(void **)((u8 *)(a) + 0x174))
#define VR_PLAYER_GUN_DRAWN(ps) (*((u8 *)(ps) + 0x8C))

static s32 vr_gun_drawn(Actor *player) {
    void *ps = VR_ACTOR_CUSTOM(player);
    return ps != NULL && VR_PLAYER_GUN_DRAWN(ps) != 0;
}

static f32 vr_sweep_sqrt(f32 v) {
    return vr_sqrt(v);
}

// Kong body tuning (units at DK's scale; the actor's model scale adjusts them).
typedef struct VrKongBody {
    f32 height;       // feet to eyes
    f32 radius;       // body sphere
    f32 arm;          // arm reach multiplier
    f32 launch;       // launch multiplier
} VrKongBody;

static const VrKongBody vr_kong_bodies[5] = {
    { 21.0f, 5.0f, 1.00f, 1.00f },  // DK: the baseline
    { 16.0f, 4.0f, 0.95f, 1.15f },  // Diddy: lighter, faster
    { 20.0f, 4.5f, 1.25f, 0.95f },  // Lanky: long arms
    { 15.0f, 3.8f, 0.90f, 1.10f },  // Tiny: small
    { 23.0f, 6.0f, 1.05f, 0.85f },  // Chunky: heavy
};

static const VrKongBody *vr_kong(void) {
    s32 k = current_character_index[0];
    if (k < 0 || k > 4) {
        k = 0;
    }
    return &vr_kong_bodies[k];
}

// Play space (metres, x right, y up, z back) to world units around the body heading.
static gtl_vec3 vr_play_to_world(const f32 v[3], f32 k) {
    gtl_vec3 o;
    f32 s = vr_sin(vr_yaw), c = vr_cos(vr_yaw);
    f32 fwd = -v[2] * k, right = v[0] * k;
    // forward = (sin, cos), right = (-cos, sin)
    o.x = s * fwd - c * right;
    o.y = v[1] * k;
    o.z = c * fwd + s * right;
    return o;
}

// Player states (Actor+0x154). Only numbers the recomp's own decomp confirms are used:
// 0x0C is plain movement (set after a Kong swap, after Mini Monkey ends, after the Ice Tomato
// game, each followed by func_global_asm_806CFF9C "resume"); 7 / 0xA / 0xB are the Tag Barrel's
// lean states (its input handler turns stick X into 0xA / 0xB). Climbing is found by behaviour
// instead (see vr_update_climb).
// (VR_STATE_NORMAL and VR_STATE_AIR are defined with the throw state above.)

static s32 vr_in_tag_barrel(Actor *player) {
    u8 st = player->control_state;
    return st == 7 || st == 0xA || st == 0xB;
}

static s32 vr_body_mode;
static s32 vr_body_mode_logged = -1;
static s32 vr_hang_frames;         // frames off the ground with no vertical speed and no hand held
static s32 vr_air_frames;          // frames off the ground with no hand on anything
static f32 vr_last_head[3];        // last frame's tracked head, to spot recentres
static s32 vr_surface_swim;        // swimming at the surface: paddle flat
static f32 vr_water_lift;          // eyes raised over the water while swimming at the surface
static s32 vr_climb_seen;          // frames since the game last held the Kong on something
static f32 vr_climb_x, vr_climb_z; // where that was
static s32 vr_grip_climbing;       // a grip holds on there

static s32 vr_should_drive(Actor *player) {
    s32 scene = vr_current_scene();
    vr_body_mode = VR_BODY_OFF;
    if (!vr_hands.valid || !vr_hands.gorilla || player == NULL) {
        return 0;
    }
    if (cc_number_of_players > 1) {
        return 0;
    }
    if (scene != VR_SCENE_GAMEPLAY) {
        // the game's own first person (scope, aiming minigames) keeps the base port's head aim
        return 0;
    }
    if (current_character_index[0] > 4 || vr_vehicle_map()) {
        // Rambi, Enguarde, carts and races
        vr_body_mode = VR_BODY_THIRD_PERSON;
        return 0;
    }
    if (vr_in_tag_barrel(player)) {
        vr_body_mode = VR_BODY_BARREL;
        return 0;
    }
    // Hanging on a vine, rope, tree, ladder or ledge: off the ground, not falling, nothing held
    // by the hands, for a few frames. First person stays; the hands climb instead of driving.
    vr_body_mode = (vr_hang_frames >= 6) ? VR_BODY_CLIMB : VR_BODY_ARMS;
    return 1;
}

// Tag Barrel: lean left or right to turn it, both hands flung up to pick (A).
static void vr_barrel_assist(void) {
    f32 lean = vr_hands.head[0] / 0.12f;
    vr_gst_out.hold = 0;
    vr_gst_out.events = 0;
    vr_gst_out.aim_valid = 0;
    if (lean > 1.0f) {
        lean = 1.0f;
    }
    if (lean < -1.0f) {
        lean = -1.0f;
    }
    vr_gst_out.stick_valid = (lean > 0.35f || lean < -0.35f);
    vr_gst_out.stick[0] = lean;
    vr_gst_out.stick[1] = 0.0f;
    if (vr_hands.hand_vel[0][1] > 2.5f && vr_hands.hand_vel[1][1] > 2.5f) {
        vr_gst_out.hold = GST_N64_A;
    }
}

static void vr_setup_params(Actor *player) {
    const VrKongBody *kb = vr_kong();
    f32 scale = 1.0f;
    if (player->animation_state != NULL && player->animation_state->scale_y > 0.0f) {
        scale = player->animation_state->scale_y / 0.15f;  // Hunky Chunky, Mini Monkey
    }
    gtl_default_params(&vr_params);
    vr_params.body_radius = kb->radius * scale;
    vr_params.hand_radius = 1.2f * scale;
    if (vr_hands.match_height) {
        // Optional: the game's floor at your real floor, the world sized so your eyes are at the
        // Kong's eye height.
        f32 standing = -vr_hands.floor_y;   // eyes above the floor at the recentre, metres
        if (!(standing > 0.8f)) {
            standing = 0.8f;
        }
        if (standing > 2.3f) {
            standing = 2.3f;
        }
        vr_ws = kb->height * scale / standing;
        vr_height = standing * vr_ws;
    } else {
        // Gorilla Tag (the default): your eyes are at the Kong's eye height over its feet. With
        // nothing held you drop until the Kong stands on the ground; hands on the ground hold you
        // up, and pushing down lifts you. The real floor doesn't matter.
        vr_ws = vr_hands.world_scale;
        vr_height = kb->height * scale;
    }
    if (!(vr_ws > 1.0f)) {
        vr_ws = 1.0f;
    }
    vr_params.head_height = vr_height - vr_params.body_radius;
    // GorillaLocomotion's own numbers (its Gorilla Rig prefab): 1.5 m of arm, 0.4 m/s before a
    // push throws you, 1 m before a stuck hand lets go, 8 frames of velocity history at ~90 Hz
    // (3 at the game's 30 Hz); the multiplier and cap are the jump_multiplier and
    // max_jump_speed settings (1.1, 6.5 m/s)
    vr_params.arm_length = 1.5f * vr_ws * vr_hands.arm_scale * kb->arm;
    vr_params.jump_multiplier = vr_hands.jump_multiplier * kb->launch;
    vr_params.max_jump_speed = vr_hands.max_jump_speed * vr_ws;
    vr_params.min_launch_speed = 0.4f * vr_ws;
    vr_params.unstick_distance = 1.0f * vr_ws;
    vr_params.history_frames = 3;
    vr_params.gravity = 0.0f;  // the game's own
    vr_sweep_ctx.player = player;
    vr_sweep_ctx.wall_window = 26.0f;  // game units: steps up to about DK's height are walls
}

static void vr_build_input(gtl_input *in, f32 dt) {
    f32 k = vr_ws * vr_hands.arm_scale;
    s32 h;
    in->head = vr_play_to_world(vr_hands.head, vr_ws);
    for (h = 0; h < 2; h++) {
        // the hand's offset from the head gets the arm scale, the head itself does not
        f32 rel[3];
        gtl_vec3 r;
        rel[0] = vr_hands.hand[h][0] - vr_hands.head[0];
        rel[1] = vr_hands.hand[h][1] - vr_hands.head[1];
        rel[2] = vr_hands.hand[h][2] - vr_hands.head[2];
        r = vr_play_to_world(rel, k);
        in->hand[h].x = in->head.x + r.x;
        in->hand[h].y = in->head.y + r.y;
        in->hand[h].z = in->head.z + r.z;
        in->hand_enabled[h] = 1;
        in->hand_hold[h] = 0;
    }
    in->dt = dt;
    in->external_flight = 1;
    in->mode = GTL_MODE_AIR_GROUND;
}

static gtl_vec3 vr_feet_to_body(Actor *p) {
    gtl_vec3 b;
    b.x = p->x_position;
    b.y = p->y_position + vr_params.body_radius;
    b.z = p->z_position;
    return b;
}

static void vr_gorilla_gestures(Actor *player, const gtl_input *loco_in, f32 dt) {
    gst_input g;
    s32 h, k;
    (void)loco_in;

    vr_gst_out.hold = 0;
    vr_gst_out.events = 0;
    vr_gst_out.stick_valid = 0;
    vr_gst_out.aim_valid = 0;
    gst_default_params(&vr_gst_params);
    vr_gst_params.dominant = vr_hands.dominant;
    vr_gst_params.crouch_by_ducking = vr_hands.duck_crouch;
    for (k = 0; k < 3; k++) {
        g.head[k] = vr_hands.head[k];
        for (h = 0; h < 2; h++) {
            g.hand[h][k] = vr_hands.hand[h][k];
            g.hand_vel[h][k] = vr_hands.hand_vel[h][k];
        }
    }
    g.head_yaw = vr_hands.head_yaw;
    for (h = 0; h < 2; h++) {
        g.grip[h] = vr_hands.grip[h];
        g.trigger[h] = vr_hands.trigger[h];
        g.touching[h] = vr_loco.hand[h].touching;
    }
    g.airborne = !(VR_ACTOR_CONTACT(player) & 1) && !vr_loco.hand[0].touching && !vr_loco.hand[1].touching;
    g.in_water = vr_in_water || (VR_ACTOR_CONTACT(player) & 2) != 0;
    g.gun_out = vr_gun_drawn(player);
    // gestures off still leaves the stick actions
    g.no_motion = !vr_hands.gestures;
    // right-stick flicks: gun, orange, instrument, Kong special. Not while the left stick moves
    // you (stick_move), steers a long fall or climbs: then the right stick turns as usual. While
    // they're on, the host moves turning and C-up / C-down to the left stick.
    g.stick[0] = vr_hands.stick[1][0];
    g.stick[1] = vr_hands.stick[1][1];
    g.stick_actions = !vr_hands.stick_move && vr_body_mode == VR_BODY_ARMS && vr_air_frames <= 24;
    g.stick_swaps_kong = vr_hands.tag_anywhere;
    g.dt = dt;
    gst_step_frame(&vr_gst, &vr_gst_params, &g, &vr_gst_out);
    if (vr_body_mode != VR_BODY_CLIMB) {
        // hand-over-hand only means "climb" on something climbable; elsewhere it would walk
        vr_gst_out.stick_valid = 0;
        vr_gst_out.events &= ~GST_EV_CLIMB;
    }
    if (vr_gst_out.events & ~(GST_EV_CROUCH | GST_EV_CLIMB | GST_EV_TWIRL)) {
        vr_tag_buzz = 1;  // a short buzz on both hands: the gesture registered
        recomp_printf("[vr-gesture] events %x hold %x (stick %.1f %.1f, gun %d)\n", vr_gst_out.events, vr_gst_out.hold,
            vr_hands.stick[1][0], vr_hands.stick[1][1], g.gun_out);
    }
}

// ---------------------------------------------------------------------------------------------
// Tag Anywhere (experimental, `tag_anywhere`): flick the right stick left / right to swap to the previous / next
// freed Kong without a Tag Barrel. The swap follows the DK64 Randomizer's (MIT, base-hack
// src/fixes/tag_anywhere.c): set the hand models, point the HUD at the new Kong, swap the
// character, resume normal movement. Only allowed in plain standing play, so nothing the swap
// doesn't handle is going on.

extern u8 isFlagSet(s16 flagIndex, u8 flagType);
void func_global_asm_806C8E58(s16 actorType);           // swap the player's character (tagKong)
void func_global_asm_806CFF9C(Actor *arg0);             // resume normal movement
void func_global_asm_806F09F0(Actor *arg0, u16 arg1);   // hand models for a Kong

#define VR_HUD_POINTER 0x80754280
#define VR_HUD_ITEMS 0x1F
#define VR_PLAYER_COLLISION_QUEUE(a) (*(void **)((u8 *)(a) + 0x13C))
#define VR_PLAYER_POWER_FLAGS(a) (*(s32 *)((u8 *)(a) + 0x370))   // Strong Kong, sprint, Gorilla Gone, seasick
#define VR_PLAYER_NEW_KONG(a) (*((u8 *)(a) + 0x36F))
#define VR_PLAYER_STATE_HELD(ps) (*(void **)((u8 *)(ps) + 0x1AC))

static const s16 vr_kong_freed_flag[5] = { 0, 6, 0x46, 0x42, 0x75 };  // sPauseKongTables
static s32 vr_tag_cooldown;
static f32 vr_tag_stick_last;
static s32 vr_diag_tag_seen;

static s32 vr_kong_freed(s32 k) {
    return k == 0 || isFlagSet(vr_kong_freed_flag[k], 0);
}

static s32 vr_tag_map_allowed(void) {
    if (vr_vehicle_map()) {
        return 0;
    }
    switch (current_map) {
        case MAP_HELM: case MAP_HELM_LOBBY: case MAP_TROFF_N_SCOFF: case MAP_GALLEON_KROOLS_SHIP:
        case MAP_KROOL_FIGHT_DK_PHASE: case MAP_KROOL_FIGHT_DIDDY_PHASE: case MAP_KROOL_FIGHT_LANKY_PHASE:
        case MAP_KROOL_FIGHT_TINY_PHASE: case MAP_KROOL_FIGHT_CHUNKY_PHASE: case MAP_KROOLS_SHOE:
        case MAP_KROOLS_ARENA: case MAP_KROOL_BARREL_LANKY_MAZE: case MAP_KROOL_BARREL_DK_TARGET_GAME:
        case MAP_KROOL_BARREL_TINY_MUSHROOM_GAME: case MAP_KROOL_BARREL_DIDDY_KREMLING_GAME:
        case MAP_KROOL_BARREL_DIDDY_ROCKETBARREL_GAME: case MAP_KROOL_BARREL_LANKY_SHOOTING_GAME:
        case MAP_KROOL_BARREL_CHUNKY_HIDDEN_KREMLING_GAME: case MAP_KROOL_BARREL_TINY_PONY_TAIL_TWIRL_GAME:
        case MAP_KROOL_BARREL_CHUNKY_SHOOTING_GAME: case MAP_KROOL_BARREL_DK_RAMBI_GAME:
            return 0;
        default:
            return 1;
    }
}

static s32 vr_tag_allowed(Actor *p) {
    void *ps = VR_ACTOR_CUSTOM(p);
    f32 scale = p->animation_state != NULL ? p->animation_state->scale_y : 0.15f;
    if (is_cutscene_active || vr_body_mode != VR_BODY_ARMS || vr_in_water) {
        return 0;
    }
    if (current_character_index[0] > 4) {
        return 0;
    }
    {
        // On the ground, or held just off it by the hands (the game calls that falling): within
        // 40 units of the floor, like the Randomizer's own rule, and not moving up or down.
        f32 floor_y;
        if (!(VR_ACTOR_CONTACT(p) & 1)) {
            if (!(vr_loco.hand[0].touching || vr_loco.hand[1].touching) || p->y_velocity < -0.01f || p->y_velocity > 0.01f) {
                return 0;
            }
            if (!vr_floor_at(p, p->x_position, p->z_position, p->y_position + 5.0f, &floor_y) || p->y_position - floor_y > 40.0f) {
                return 0;
            }
        }
    }
    if (ps == NULL || VR_PLAYER_GUN_DRAWN(ps) || VR_PLAYER_STATE_HELD(ps) != NULL) {
        return 0;
    }
    if (VR_PLAYER_COLLISION_QUEUE(p) != NULL || (VR_PLAYER_POWER_FLAGS(p) & 0x170)) {
        return 0;
    }
    if (scale < 0.14f || scale > 0.16f) {
        return 0;   // Mini Monkey, Hunky Chunky
    }
    if (p->control_state != VR_STATE_NORMAL && p->control_state != VR_STATE_AIR) {
        return 0;
    }
    return vr_tag_map_allowed();
}

static void vr_tag_to(Actor *p, s32 next) {
    u8 *hud = *(u8 **)VR_HUD_POINTER;
    s32 i;
    func_global_asm_806F09F0(p, (u16)(next + 2));
    if (hud != NULL) {
        for (i = 0; i < VR_HUD_ITEMS; i++) {
            hud[i * 0x30 + 0x2F] = (u8)next;
        }
    }
    func_global_asm_806C8E58((s16)(next + 2));
    func_global_asm_806CFF9C(p);
    if (p->unkB8 > 140.0f) {
        p->unkB8 = 140.0f;
    }
    VR_PLAYER_NEW_KONG(p) = (u8)(next + 2);
    vr_tag_buzz = 1;
    recomp_printf("[vr-tag] swapped to kong %d\n", next);
}

static void vr_tag_anywhere(Actor *p) {
    f32 x = vr_hands.stick[1][0];   // right stick left / right, with the other stick actions
    s32 dir = 0, k, i, next;
    if (vr_tag_cooldown > 0) {
        vr_tag_cooldown--;
    }
    if (vr_hands.diag_tag != vr_diag_tag_seen) {
        vr_diag_tag_seen = vr_hands.diag_tag;
        if (vr_hands.diag_tag != 0) {
            dir = 2;   // diagnostics: next Kong, freed or not
        }
    }
    if (dir == 0 && vr_hands.tag_anywhere && !vr_hands.stick_move) {
        if (x > 0.7f && vr_tag_stick_last <= 0.3f) {
            dir = 1;
        } else if (x < -0.7f && vr_tag_stick_last >= -0.3f) {
            dir = -1;
        }
    }
    vr_tag_stick_last = x;
    if (dir == 0 || vr_tag_cooldown > 0) {
        return;
    }
    if (!vr_tag_allowed(p)) {
        recomp_printf("[vr-tag] not now (state %d)\n", p->control_state);
        return;
    }
    k = current_character_index[0];
    next = k;
    for (i = 0; i < 4; i++) {
        next = (next + (dir == -1 ? 4 : 1)) % 5;
        if (dir == 2 || vr_kong_freed(next)) {
            break;
        }
    }
    if (next == k) {
        return;
    }
    vr_tag_cooldown = 20;
    vr_tag_to(p, next);
}

// --- Testing tab (VR menu): warp anywhere, unlock everything ---------------------------------
// The host bumps a counter for each request; the game acts once per change, at the same point
// in the frame upstream's debug warp menu uses. A request waits while there's no player to act
// on (loading, title screen), so a warp asked for mid-load still happens.

void func_global_asm_805FF378(Maps nextMap, s32 nextExit);   // start a map transition
void setFlag(s16 flagIndex, u8 newValue, u8 flagType);
extern u8 D_global_asm_807FC950[];   // PlayerProgress: 6 x 0x5E bytes of Kong progress, then shared items

#define VR_PROG_KONG(k) (D_global_asm_807FC950 + (k) * 0x5E)
#define VR_PROG_U16(off) (*(u16 *)(D_global_asm_807FC950 + (off)))

static s32 vr_warp_seen;
static s32 vr_unlock_seen;
static s32 vr_test_waiting_logged;

static const s16 vr_unlock_flags[] = {
    6, 0x42, 0x46, 0x75,            // Diddy, Tiny, Lanky, Chunky freed
    0x179,                          // Fairy camera and Shockwave
    0x17F, 0x180,                   // training spawned, first Simian Slam given
    0x182, 0x183, 0x184, 0x185,     // dive, vines, oranges, barrels
    0x187,                          // all training complete
};

static void vr_unlock_everything(void) {
    s32 k, i;
    u8 *c;
    for (i = 0; i < (s32)(sizeof(vr_unlock_flags) / sizeof(vr_unlock_flags[0])); i++) {
        setFlag(vr_unlock_flags[i], 1, 0);
    }
    for (k = 0; k < 5; k++) {
        c = VR_PROG_KONG(k);
        c[0] = 7;      // Cranky's special moves, all three
        c[1] = 3;      // Super Duper Simian Slam
        c[2] = 7;      // gun, homing ammo, sniper scope
        c[3] = 2;      // second ammo belt
        c[4] = 15;     // instrument and all three upgrades
        *(u16 *)(c + 8) = 20;   // instrument energy
        if (*(u16 *)(c + 6) < 50) {
            *(u16 *)(c + 6) = 50;   // coins
        }
    }
    VR_PROG_U16(0x2F0) = 200;   // standard ammo
    VR_PROG_U16(0x2F2) = 200;   // homing ammo
    VR_PROG_U16(0x2F4) = 20;    // oranges
    VR_PROG_U16(0x2F6) = 20 * 150;   // crystal coconuts (150 per one on screen)
    VR_PROG_U16(0x2F8) = 10;    // film
    D_global_asm_807FC950[0x2FC] = 3;    // melons
    D_global_asm_807FC950[0x2FB] = 12;   // health, 4 per melon
    vr_tag_buzz = 1;
    recomp_printf("[vr-test] unlocked all Kongs, moves and items\n");
}

static void vr_test_requests(Actor *player) {
    s32 ready = (player != NULL) && (is_cutscene_active == 0) && (cc_number_of_players == 1);
    if ((vr_hands.warp_req == vr_warp_seen) && (vr_hands.unlock_req == vr_unlock_seen)) {
        return;
    }
    if (!ready) {
        if (!vr_test_waiting_logged) {
            vr_test_waiting_logged = 1;
            recomp_printf("[vr-test] waiting for play to act on the request\n");
        }
        return;
    }
    vr_test_waiting_logged = 0;
    if (vr_hands.unlock_req != vr_unlock_seen) {
        vr_unlock_seen = vr_hands.unlock_req;
        vr_unlock_everything();
    }
    if (vr_hands.warp_req != vr_warp_seen) {
        vr_warp_seen = vr_hands.warp_req;
        recomp_printf("[vr-test] warp to map %d exit %d (from map %d)\n", vr_hands.warp_map, vr_hands.warp_exit, current_map);
        func_global_asm_805FF378((Maps)vr_hands.warp_map, vr_hands.warp_exit);
    }
}

// ---------------------------------------------------------------------------------------------
// Hands into barrels. The game only lets the player's body enter a barrel (Tag Barrels, bonus
// and training barrels, blast barrels, Mini Monkey / Hunky Chunky / Strong Kong / Orangstand
// barrels). In Gorilla Tag you'd reach out and touch it, so a hand that touches one puts the
// body in it the way a jump would: the game lets a barrel take the player only while a jump
// animation is playing (the player's interaction type comes from its animation frames), so the
// hand starts a jump (A, from the host) and the body is held at the barrel's centre until the
// barrel takes it (Tag Barrel state 7, a bonus barrel's map change) or a second passes.

extern Actor *actor_list[256];
extern s16 actor_count;
static s32 vr_barrel_cooldown;
static Actor *vr_barrel_target;  // the barrel a hand touched
static s32 vr_barrel_hold;        // frames left holding the body in it

static s32 vr_is_barrel(s32 type) {
    switch (type) {
        case ACTOR_TAG_BARREL:
        case ACTOR_BONUS_BARREL:
        case ACTOR_BONUS_BARREL_HELM:
        case ACTOR_BONUS_TRAINING:
        case ACTOR_CANNON_BARREL:
        case ACTOR_MINI_MONKEY_BARREL:
        case ACTOR_HUNKY_CHUNKY_BARREL:
        case ACTOR_HUNKY_CHUNKY_BARREL_DOGADON:
        case ACTOR_STRONG_KONG_BARREL:
        case ACTOR_ORANGSTAND_SPRINT_BARREL:
        case ACTOR_ROCKETBARREL:
            return 1;
        default:
            return 0;
    }
}

static void vr_hand_barrels(Actor *player) {
    s32 i, h, n = actor_count;
    if (vr_barrel_hold > 0) {
        Actor *a = vr_barrel_target;
        s32 alive = 0;
        for (i = 0; i < n && i < 256; i++) {
            if (actor_list[i] == a) {
                alive = 1;
            }
        }
        vr_barrel_hold--;
        if (!alive || vr_in_tag_barrel(player) || vr_barrel_hold == 0) {
            recomp_printf("[vr-loco] barrel %s\n", vr_in_tag_barrel(player) ? "took us" : (alive ? "didn't take us" : "gone (taken)"));
            vr_barrel_hold = 0;
            vr_barrel_target = NULL;
            return;
        }
        // in the jump, held at its centre so its box and ours overlap
        player->x_position = a->x_position;
        player->y_position = a->y_position + 8.0f;
        player->z_position = a->z_position;
        player->unkB8 = 0.0f;
        return;
    }
    if (vr_barrel_cooldown > 0) {
        vr_barrel_cooldown--;
        return;
    }
    if (n > 256) {
        n = 256;
    }
    for (i = 0; i < n; i++) {
        Actor *a = actor_list[i];
        if (a == NULL || a == player || (u32)a == 0xFFFFFFFF || !vr_is_barrel(a->unk58)) {
            continue;
        }
        // hidden (state 0x14) or not taking anyone right now (interaction type isn't 1)
        if (a->control_state == 0x14 || !(a->interactable & 1)) {
            continue;
        }
        for (h = 0; h < 2; h++) {
            // a barrel stands on its position, about DK's height tall
            f32 dx = vr_loco.hand[h].pos.x - a->x_position, dz = vr_loco.hand[h].pos.z - a->z_position;
            f32 dy = vr_loco.hand[h].pos.y - a->y_position;
            f32 r = 12.0f + vr_params.hand_radius;
            if (dx * dx + dz * dz < r * r && dy > -4.0f && dy < 32.0f) {
                recomp_printf("[vr-loco] hand %d on a barrel (actor type %d): jump in\n", h, a->unk58);
                vr_barrel_target = a;
                vr_barrel_hold = 30;
                vr_barrel_cooldown = 45;
                vr_launch_count++;     // the host's short A press: the jump
                vr_fly_end("into a barrel");
                vr_initialised = 0;    // the hands start fresh wherever the barrel takes us
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Cutscenes through the Kong's eyes (immersive cutscenes only). A right stick click switches any
// cutscene between the game's camera and first person; the shops' (Cranky, Funky, Candy, Snide)
// start in first person with `cutscene_pov` on. The view faces the way the Kong faced when it
// started, so the game's camera cuts don't turn you.

static s32 vr_pov;
static s32 vr_pov_was;
static f32 vr_pov_yaw;

static s32 vr_pov_map_default(void) {
    return current_map == MAP_CRANKYS_LAB || current_map == MAP_FUNKYS_STORE || current_map == MAP_CANDYS_MUSIC_SHOP
        || current_map == MAP_SNIDES_HQ;
}

static void vr_update_pov(Actor *player) {
    s32 want = 0;
    if (player != NULL && vr_hands.hide_cutscene_bars && vr_current_scene() == VR_SCENE_CUTSCENE
        && cc_number_of_players == 1 && current_character_index[0] >= 0 && current_character_index[0] <= 4) {
        want = (vr_hands.pov_choice >= 0) ? vr_hands.pov_choice : (vr_hands.cutscene_pov && vr_pov_map_default());
    }
    if (want && !vr_pov_was) {
        vr_pov_yaw = vr_game_to_heading(player->y_rotation);
        recomp_printf("[vr-loco] cutscene in first person (map %d)\n", current_map);
    } else if (!want && vr_pov_was) {
        recomp_printf("[vr-loco] cutscene in third person\n");
    }
    vr_pov = want;
    vr_pov_was = want;
}

void vr_gorilla_pre_frame(void) {
    Actor *player;
    gtl_input in;
    gtl_events ev;
    gtl_vec3 before;
    f32 dt;
    s32 turn;

    vr_drive = 0;
    vr_active = 0;
    if (!recomp_vr_is_active() || character_change_array == NULL) {
        vr_initialised = 0;
        vr_body_mode = VR_BODY_OFF;
        vr_gst_out.hold = 0;
        vr_gst_out.stick_valid = 0;
        return;
    }
    recomp_vr_get_hands(&vr_hands);
    player = character_change_array[0].playerPointer;
    vr_test_requests(player);
    vr_update_pov(player);
    vr_gst_out.hold = 0;
    vr_gst_out.stick_valid = 0;
    if (!vr_should_drive(player)) {
        vr_initialised = 0;
        vr_air_frames = 0;
        vr_fly_end("arm locomotion paused");
        if (vr_body_mode == VR_BODY_BARREL && vr_hands.gestures) {
            vr_barrel_assist();
        }
        if (vr_body_mode != vr_body_mode_logged) {
            vr_body_mode_logged = vr_body_mode;
            recomp_printf("[vr-loco] mode %d (map %d, kong %d, state %d)\n", vr_body_mode, current_map,
                current_character_index[0], player != NULL ? player->control_state : -1);
        }
        return;
    }
    if (vr_body_mode != vr_body_mode_logged) {
        vr_body_mode_logged = vr_body_mode;
        recomp_printf("[vr-loco] mode %d (map %d, kong %d)\n", vr_body_mode, current_map, current_character_index[0]);
    }

    dt = 1.0f / 30.0f;
    vr_setup_params(player);
    vr_world.user = &vr_sweep_ctx;
    vr_world.sweep = vr_sweep;
    vr_world.sqrt_fn = vr_sweep_sqrt;
    vr_build_input(&in, dt);

    // snap / smooth turns turn the body; the host shows them at once
    turn = recomp_vr_take_turn();
    if (turn != 0) {
        vr_yaw = vr_wrap(vr_yaw - (f32)turn * (VR_PI / 180.0f));
        vr_build_input(&in, dt);
    }
    {
        // A snap turn swings the hands round the head, and a recentre moves them all at once;
        // either would drag a held hand or read as a throw. Start the hands fresh instead.
        f32 dx = vr_hands.head[0] - vr_last_head[0], dy = vr_hands.head[1] - vr_last_head[1];
        f32 dz = vr_hands.head[2] - vr_last_head[2];
        if (vr_initialised && player == vr_player_seen && (turn != 0 || dx * dx + dy * dy + dz * dz > 0.35f * 0.35f)) {
            gtl_reset(&vr_loco, vr_feet_to_body(player), &in);
            if (turn == 0) {
                recomp_printf("[vr-loco] head jumped %.2f m (recentre), hands reset\n", vr_sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
        vr_last_head[0] = vr_hands.head[0];
        vr_last_head[1] = vr_hands.head[1];
        vr_last_head[2] = vr_hands.head[2];
    }

    if (!vr_initialised || player != vr_player_seen) {
        vr_yaw = vr_game_to_heading(player->y_rotation);
        vr_build_input(&in, dt);
        gtl_reset(&vr_loco, vr_feet_to_body(player), &in);
        vr_initialised = 1;
        vr_player_seen = player;
        recomp_printf("[vr-loco] body reset at %.1f %.1f %.1f, kong %d\n", player->x_position,
            player->y_position, player->z_position, current_character_index[0]);
    }

    // The game is authoritative for where the body is: walls, platforms, warps.
    before = vr_feet_to_body(player);
    {
        f32 dx = before.x - vr_loco.body.x, dy = before.y - vr_loco.body.y, dz = before.z - vr_loco.body.z;
        if (dx * dx + dy * dy + dz * dz > 60.0f * 60.0f) {
            gtl_reset(&vr_loco, before, &in);  // warped (loading zone, warp pad, cutscene)
            vr_fly_end("warped");
        } else {
            vr_loco.body = before;
        }
    }

    if (vr_flying) {
        // The game's own collision ran after last frame's throw step (room walls, ceilings,
        // floors our sweep doesn't see). Where it pushed the body back, lose the speed that went
        // that way, so a wall stops you instead of being pushed into forever.
        gtl_vec3 *v = &vr_loco.velocity;
        f32 cx = before.x - vr_fly_expect.x, cz = before.z - vr_fly_expect.z;
        f32 cy = before.y - vr_fly_expect.y;
        f32 l = vr_sqrt(cx * cx + cz * cz);
        if (l > 0.25f) {
            f32 into = (v->x * cx + v->z * cz) / l;
            if (into < 0.0f) {
                v->x -= cx / l * into;
                v->z -= cz / l * into;
            }
        }
        if ((cy > 0.25f && v->y < 0.0f) || (cy < -0.25f && v->y > 0.0f)) {
            v->y = 0.0f;  // landed on something, or hit a ceiling
        }
        vr_fly_frames++;
        if (vr_in_tag_barrel(player) || !vr_fly_state_ok(player->control_state)) {
            recomp_printf("[vr-loco] game took the player (state %d)\n", player->control_state);
            vr_fly_end("game state");
        } else if (vr_fly_frames > 30 * 15) {
            vr_fly_end("too long");
        }
    }

    // Under water? The floor query also reports the water surface over that spot.
    {
        f32 floor_y, water_y = 0.0f;
        f32 head_y = vr_loco.body.y + vr_params.head_height;
        // the floor under the body (the water level comes with it); starting far overhead
        // found roofs indoors, which have no water, so swimming was never noticed
        vr_floor_at(player, vr_loco.body.x, vr_loco.body.z, head_y + 10.0f, &floor_y);
        s32 has_water = func_global_asm_8066715C(&water_y);
        // Swimming at the surface the game holds the Kong's feet about 35 units under the water,
        // so its eyes (21 up) would be under it: lift the eyes and the hands to just above the
        // water then. Deeper than that is diving, at the normal height.
        vr_water_lift = 0.0f;
        if (has_water && (water_y - player->y_position) > 0.0f && (water_y - player->y_position) < 45.0f
            && head_y < water_y + 3.0f) {
            vr_water_lift = water_y + 3.0f - head_y;
            vr_params.head_height += vr_water_lift;
            head_y += vr_water_lift;
        }
        // swimming at the surface keeps the head just above the water, so count a body that's
        // in it up to the eyes too, and only paddle along the surface then
        vr_surface_swim = has_water && head_y >= water_y && vr_loco.body.y < water_y;
        if (has_water && (head_y < water_y || vr_surface_swim)) {
            in.mode = GTL_MODE_WATER;
            in.external_flight = 0;
            vr_params.gravity = 0.0f;
            vr_params.buoyancy = 0.0f;
            if (!vr_in_water) {
                recomp_printf("[vr-loco] in water (surface %.1f)\n", water_y);
            }
            vr_in_water = 1;
        } else {
            vr_in_water = 0;
        }
    }

    // Grip climbing (Gorilla Tag style): on a tree, ladder, vine or ledge the game holds the
    // Kong; a closed grip there holds that hand where it is and pulls move the body directly
    // (through the movement patch, like any held hand). Open hands leave it to the game's own
    // climbing (the stick and the overhead-pull gesture), and letting go mid-pull throws.
    if (vr_body_mode == VR_BODY_CLIMB) {
        vr_climb_seen = 90;                    // three seconds to start gripping after it lets go
        vr_climb_x = player->x_position;
        vr_climb_z = player->z_position;
    } else if (vr_climb_seen > 0) {
        vr_climb_seen--;
    }
    {
        f32 dx = player->x_position - vr_climb_x, dz = player->z_position - vr_climb_z;
        s32 near = (vr_climb_seen > 0 || vr_grip_climbing) && (dx * dx + dz * dz < 40.0f * 40.0f);
        s32 g0 = vr_hands.grip[0] > 0.6f, g1 = vr_hands.grip[1] > 0.6f;
        if (near && (g0 || g1) && !vr_gun_drawn(player)) {
            if (!vr_grip_climbing) {
                recomp_printf("[vr-loco] grip climbing\n");
            }
            vr_grip_climbing = 1;
            in.hand_hold[0] = g0;
            in.hand_hold[1] = g1;
            in.mode = GTL_MODE_AIR_GROUND;
            in.hand_enabled[0] = in.hand_enabled[1] = 1;
            vr_body_mode = VR_BODY_ARMS;       // the hands drive now, not the game's climb
            vr_fly_end("climbing");
        } else {
            if (vr_grip_climbing) {
                recomp_printf("[vr-loco] grip climbing over\n");
            }
            vr_grip_climbing = 0;
            if (vr_body_mode == VR_BODY_CLIMB) {
                // the game's own climbing: the hands only steer it (stick)
                in.hand_enabled[0] = in.hand_enabled[1] = 0;
                in.mode = GTL_MODE_FROZEN;
                vr_fly_end("climbing");
            }
        }
    }
    if (vr_in_water) {
        vr_fly_end("water");
    }
    if (vr_flying) {
        // the core flies the body: its own gravity, sliding and sweeps against the floors
        in.external_flight = 0;
        vr_params.gravity = VR_FLY_GRAVITY * vr_ws;
        vr_params.terminal_velocity = 30.0f * vr_ws;
        vr_params.ground_friction = 4.0f;
    }
    gtl_step(&vr_loco, &vr_params, &vr_world, &in, &ev);

    vr_disp.x = vr_loco.body.x - before.x;
    vr_disp.y = vr_loco.body.y - before.y;
    vr_disp.z = vr_loco.body.z - before.z;
    if (vr_in_water && vr_surface_swim && vr_disp.y > 0.0f) {
        // at the surface the game keeps the player afloat; paddling only moves along it (down
        // still dives)
        vr_disp.y = 0.0f;
        vr_loco.body.y = before.y;
    }
    vr_drive = vr_body_mode == VR_BODY_ARMS && (vr_loco.hand[0].touching || vr_loco.hand[1].touching
        || vr_in_water || vr_flying || (vr_disp.x * vr_disp.x + vr_disp.z * vr_disp.z) > 0.0001f);
    vr_active = 1;

    if (ev.launched) {
        f32 sp = vr_sqrt(ev.launch_velocity.x * ev.launch_velocity.x + ev.launch_velocity.y * ev.launch_velocity.y
            + ev.launch_velocity.z * ev.launch_velocity.z);
        if (ev.launch_velocity.y > 0.35f * sp && (VR_ACTOR_CONTACT(player) & 1)) {
            // thrown upwards off the ground (not in the air, where A is Tiny's twirl and the
            // like): the host presses A so the game plays its jump
            // (animation, sound, leaves the ground state); the throw's speed stays ours
            vr_launch_count++;
        }
        vr_flying = 1;
        vr_fly_frames = 0;
        vr_fly_state_count = 0;
        vr_fly_states[vr_fly_state_count++] = player->control_state;
        vr_body.launch_speed = sp;
        recomp_printf("[vr-loco] launch %.1f %.1f %.1f (%.2f m/s)\n", ev.launch_velocity.x, ev.launch_velocity.y,
            ev.launch_velocity.z, sp / vr_ws);
    } else if (vr_flying) {
        f32 vh2 = vr_loco.velocity.x * vr_loco.velocity.x + vr_loco.velocity.z * vr_loco.velocity.z;
        f32 stop = VR_FLY_STOP * vr_ws;
        if (vr_loco.hand[0].touching || vr_loco.hand[1].touching) {
            vr_fly_end("grabbed");
        } else if (vr_loco.grounded && vr_loco.velocity.y <= 0.0f && vh2 < stop * stop) {
            vr_fly_end("landed");
        }
    }
    if (vr_flying) {
        // the movement patch puts the body here; next frame shows what the game did to it
        vr_fly_expect = vr_loco.body;
    }
    vr_hand_barrels(player);
    vr_gorilla_gestures(player, &in, dt);
    vr_tag_anywhere(player);
    if (!(VR_ACTOR_CONTACT(player) & 1) && !vr_loco.hand[0].touching && !vr_loco.hand[1].touching && !vr_in_water) {
        vr_air_frames++;
    } else {
        vr_air_frames = 0;
    }
    if (vr_air_frames > 0 && player->y_velocity > -0.01f && player->y_velocity < 0.01f && !vr_flying) {
        vr_hang_frames++;
    } else {
        vr_hang_frames = 0;
    }
    if ((vr_frame % 60) == 0) {
        recomp_printf("[vr-loco] at %.1f %.1f %.1f state %d vy %.2f holding %d%d | floor %.2f m, head %.2f, hands %.2f %.2f m, scale %.1f\n",
            player->x_position, player->y_position, player->z_position, player->control_state, player->y_velocity,
            vr_loco.hand[0].touching, vr_loco.hand[1].touching, vr_hands.floor_y, vr_hands.head[1], vr_hands.hand[0][1],
            vr_hands.hand[1][1], vr_ws);
    }
    {
        s32 h;
        for (h = 0; h < 2; h++) {
            vr_body.haptic[h] = 0;
            if (vr_tag_buzz) {
                vr_body.haptic[h] = 60;
            }
            if (ev.hand_contact[h]) {
                f32 strength = ev.contact_speed[h] / (2.0f * vr_ws);
                vr_body.haptic[h] = (s32)(20.0f + 80.0f * (strength > 1.0f ? 1.0f : strength));
                recomp_printf("[vr-loco] hand %d on surface %d\n", h, vr_loco.hand[h].surface);
            }
        }
        vr_tag_buzz = 0;
    }
}

void vr_gorilla_post_frame(void) {
    Actor *player;
    s32 h;
    f32 s, c;

    vr_frame++;
    vr_body.frame = vr_frame;
    vr_body.active = vr_active;
    if (!vr_active && vr_pov && character_change_array != NULL && character_change_array[0].playerPointer != NULL) {
        // a cutscene through the Kong's eyes: the eye point over the feet, sized as in play
        gtl_vec3 off;
        player = character_change_array[0].playerPointer;
        vr_setup_params(player);
        vr_yaw = vr_pov_yaw;   // the head offset turns with the view the host builds from it
        off = vr_play_to_world(vr_hands.head, vr_ws);
        vr_body.head[0] = player->x_position - off.x;
        vr_body.head[1] = player->y_position + vr_height;   // the head's height stays inside the body: the host adds it live
        vr_body.head[2] = player->z_position - off.z;
        vr_body.yaw = vr_pov_yaw;
        vr_body.world_scale = vr_ws;
        vr_body.mode = VR_BODY_POV;
        vr_body.press = 0;
        vr_body.stick_valid = 0;
        vr_body.stick_free = 0;
        vr_body.gun_out = 0;
        recomp_vr_publish_body(&vr_body);
        return;
    }
    if (vr_active && character_change_array != NULL && character_change_array[0].playerPointer != NULL) {
        player = character_change_array[0].playerPointer;
        // face where the head looks, so guns and moves go that way
        if (vr_body_mode == VR_BODY_ARMS && (vr_flying || player->y_velocity <= 0.0f)) {
            f32 face = vr_hands.head_yaw;
            if (vr_gst_out.aim_valid) {
                face = vr_gst_out.aim_yaw;   // punches, oranges, the gun face the move
            }
            player->y_rotation = vr_heading_to_game(vr_yaw + face);
        }
        // camera: the tracked head offset is added by the host at display rate, so publish the
        // standing eye point over the feet
        s = vr_sin(vr_yaw);
        c = vr_cos(vr_yaw);
        // the body already moved with the tracked head (room scale); the host adds the live
        // head offset at display rate, so take this frame's offset back out
        {
            gtl_vec3 off = vr_play_to_world(vr_hands.head, vr_ws);
            vr_body.head[0] = player->x_position - off.x;
            vr_body.head[1] = player->y_position + vr_height + vr_water_lift;   // the head's height stays inside the body: the host adds it live
            vr_body.head[2] = player->z_position - off.z;
        }
        vr_body.yaw = vr_yaw;
        vr_body.world_scale = vr_ws;
        for (h = 0; h < 2; h++) {
            vr_body.hand[h][0] = vr_loco.hand[h].pos.x;
            vr_body.hand[h][1] = vr_loco.hand[h].pos.y;
            vr_body.hand[h][2] = vr_loco.hand[h].pos.z;
            vr_body.touching[h] = vr_loco.hand[h].touching;
        }
        vr_body.speed = vr_sqrt(vr_disp.x * vr_disp.x + vr_disp.y * vr_disp.y + vr_disp.z * vr_disp.z) * 30.0f;
        (void)s;
        (void)c;
    }
    vr_body.launch_count = vr_launch_count;
    {
        s32 assist = vr_active || vr_body_mode == VR_BODY_BARREL;
        vr_body.press = assist ? vr_gst_out.hold : 0;
        vr_body.stick_valid = assist ? vr_gst_out.stick_valid : 0;
    }
    vr_body.stick_free = vr_active && (vr_air_frames > 24 || vr_body_mode == VR_BODY_CLIMB);
    vr_body.mode = vr_body_mode;
    vr_body.stick[0] = vr_gst_out.stick[0];
    vr_body.stick[1] = vr_gst_out.stick[1];
    vr_body.gun_out = (vr_active && character_change_array != NULL && character_change_array[0].playerPointer != NULL)
        ? vr_gun_drawn(character_change_array[0].playerPointer) : 0;
    recomp_vr_publish_body(&vr_body);
}

// @recomp: the actor movement step. For the player while a hand holds on (or paddles), the move is
// the hands' displacement instead of speed along the heading; with nothing held the head's own
// room-scale walk is added before the game's move. Everything after it (walls, floors, triggers)
// runs as normal. Otherwise unchanged from the decomp.
extern u32 D_global_asm_80744478;

RECOMP_PATCH void func_global_asm_806651FC(Actor *arg0) {
    f32 sp28;
    f32 temp_f2;
    f32 var_f0;
    f32 var_f20;

    if (vr_drive && character_change_array != NULL && arg0 == character_change_array[0].playerPointer) {
        arg0->x_position += vr_disp.x;
        arg0->y_position += vr_disp.y;
        arg0->z_position += vr_disp.z;
        vr_drive = 0;
        if (vr_loco.hand[0].touching || vr_loco.hand[1].touching || vr_in_water) {
            // held up by the hands, or paddling: the hands are the whole move
            arg0->y_velocity = 0.0f;
            arg0->unkB8 = 0.0f;
            return;
        }
        if (vr_flying) {
            // a throw: the core's flight is the whole move. Mirror its speed into the game's
            // fields so the game's air and landing logic, animations and sounds follow along.
            f32 scale_y = (arg0->animation_state != NULL) ? arg0->animation_state->scale_y : 0.15f;
            f32 per_frame = 6.0f / (scale_y * 30.0f);
            f32 vx = vr_loco.velocity.x, vz = vr_loco.velocity.z;
            f32 vh = vr_sqrt(vx * vx + vz * vz);
            arg0->y_velocity = vr_loco.velocity.y * per_frame;
            arg0->unkB8 = vh * per_frame;
            if (vh > 0.01f) {
                arg0->unkEE = vr_heading_to_game(vr_atan2(vx, vz));
            }
            return;
        }
        // nothing held: the tracked head's walk adds to the game's own move (gravity, jumps,
        // throws), it doesn't replace it
    }

    var_f20 = arg0->unkB8;
    if (arg0->unkFC != 0) {
        var_f20 = MIN(arg0->unkFA, var_f20);
    }
    if (arg0->animation_state != NULL) {
        var_f0 = arg0->animation_state->scale_y;
    } else {
        var_f0 = 0.15f;
    }
    temp_f2 = var_f0 * 0.16666666675;
    sp28 = temp_f2;
    if ((arg0->interactable & 4) && (arg0->unk6A & 4)) {
        sp28 = temp_f2 * 0.5;
    }
    var_f20 *= sp28;
    if (arg0->unk6A & 1) {
        var_f20 *= ABS(func_global_asm_80612794(arg0->unkDA));
    }
    if (arg0->object_properties_bitfield & 0x400000) {
        var_f20 *= (u32)D_global_asm_80744478 * 0.5;
    }
    arg0->x_position += var_f20 * func_global_asm_80612794(arg0->unkEE);
    arg0->z_position += var_f20 * func_global_asm_80612790(arg0->unkEE);
    arg0->y_position += arg0->y_velocity * sp28;
}

// @recomp: renderActor builds an actor's model matrix (decomp: code_35000.c). In first person the
// player's own model sits around the camera, so collapse it to a point: nothing to see, and the
// game keeps running every other part of the player as normal.
void func_global_asm_806317D4(Actor *arg0, f32 arg1[4][4], f32 arg2, f32 arg3, f32 arg4);
void func_global_asm_8063162C(Actor *arg0, f32 arg1[4][4]);
void guMtxCatF(float mf[4][4], float nf[4][4], float res[4][4]);

RECOMP_PATCH void renderActor(Actor *arg0, u8 arg1) {
    f32 sp78[4][4];
    s16 phi_a0;
    s16 temp_f2;
    s16 temp;
    s16 temp_t7;
    s16 temp_yrot;

    if ((arg0->object_properties_bitfield & 1)) {
        phi_a0 = (arg0->y_rotation - arg0->unkDC) & 0xFFF;
        if (phi_a0 >= 0x801) {
            phi_a0 = phi_a0 - 0x1000;
        }
        temp_f2 = (0x400 - arg0->unkDE) & 0xFFF;
        temp = func_global_asm_80612790(phi_a0) * temp_f2;
        temp = (temp + arg0->z_rotation) & 0xFFF;
        temp_t7 = ((s16)(func_global_asm_80612794(phi_a0) * temp_f2) + arg0->x_rotation) & 0xFFF;
        temp_yrot = arg0->y_rotation;
        if (arg1 != 0) {
            func_global_asm_806317D4(arg0, sp78, temp_t7, temp, temp_yrot);
            guMtxCatF((void *)&arg0->unkC, sp78, (void *)&arg0->unkC);
        } else {
            func_global_asm_806317D4(arg0, (void *)&arg0->unkC, temp_t7, temp, temp_yrot);
        }
    } else {
        if (arg1 != 0) {
            func_global_asm_8063162C(arg0, sp78);
            guMtxCatF((void *)&arg0->unkC, sp78, (void *)&arg0->unkC);
        } else {
            func_global_asm_8063162C(arg0, (void *)&arg0->unkC);
        }
    }

    if ((vr_active || vr_pov) && vr_hide_body && character_change_array != NULL
        && arg0 == character_change_array[0].playerPointer) {
        f32 (*m)[4] = (f32 (*)[4])(void *)&arg0->unkC;
        s32 r, c;
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                m[r][c] *= 0.0001f;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Hands: a small box per hand in the Kong's colour, drawn with the world's camera right after the
// world, so it is depth tested against it and goes through the eye reprojection like everything
// else. Brighter while it holds on to something.

extern u8 D_global_asm_807444FC;
extern u8 skip_interpolation;
extern u8 skip_persp_interp;

#define VR_HAND_UNIT 100.0f

// Up to this many boxes a frame (two per arm, three per hand), double-buffered with the game's
// display lists.
#define VR_BOXES 12
static Vtx vr_box_vtx[2][VR_BOXES][8];
static Mtx vr_box_mtx[2][VR_BOXES];

// Arm fur, per Kong.
static const u32 vr_arm_colours[5] = {
    0x6A3E1CFF, // DK brown
    0x7A4E2AFF, // Diddy
    0x8A5428FF, // Lanky
    0xA87440FF, // Tiny
    0x553618FF, // Chunky
};

// Hand skin, per Kong (light, like the Kongs' palms).
static const u32 vr_skin_colours[5] = {
    0xE0B080FF, // DK
    0xE8B888FF, // Diddy
    0xE0A878FF, // Lanky
    0xF0C098FF, // Tiny
    0xD8A070FF, // Chunky
};

static void vr_hand_box(Vtx *v, u32 rgba, s32 bright) {
    static const s8 corners[8][3] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 },
    };
    s32 i;
    for (i = 0; i < 8; i++) {
        // fake lighting: top corners lighter, bottom darker
        s32 shade = corners[i][1] > 0 ? 255 : 150;
        s32 r = ((rgba >> 24) & 0xFF) * shade / 255;
        s32 g = ((rgba >> 16) & 0xFF) * shade / 255;
        s32 b = ((rgba >> 8) & 0xFF) * shade / 255;
        if (bright) {
            r = r + (255 - r) / 2;
            g = g + (255 - g) / 2;
            b = b + (255 - b) / 2;
        }
        v[i].v.ob[0] = (s16)(corners[i][0] * VR_HAND_UNIT);
        v[i].v.ob[1] = (s16)(corners[i][1] * VR_HAND_UNIT * 0.8f);
        v[i].v.ob[2] = (s16)(corners[i][2] * VR_HAND_UNIT);
        v[i].v.flag = 0;
        v[i].v.tc[0] = 0;
        v[i].v.tc[1] = 0;
        v[i].v.cn[0] = (u8)r;
        v[i].v.cn[1] = (u8)g;
        v[i].v.cn[2] = (u8)b;
        v[i].v.cn[3] = 0xFF;
    }
}

static gtl_vec3 vr_v3(f32 x, f32 y, f32 z) {
    gtl_vec3 v;
    v.x = x; v.y = y; v.z = z;
    return v;
}
static gtl_vec3 vr_v3add(gtl_vec3 a, gtl_vec3 b) { return vr_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static gtl_vec3 vr_v3sub(gtl_vec3 a, gtl_vec3 b) { return vr_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static gtl_vec3 vr_v3mul(gtl_vec3 a, f32 k) { return vr_v3(a.x * k, a.y * k, a.z * k); }
static f32 vr_v3dot(gtl_vec3 a, gtl_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static gtl_vec3 vr_v3cross(gtl_vec3 a, gtl_vec3 b) {
    return vr_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static f32 vr_v3len(gtl_vec3 a) { return vr_sqrt(vr_v3dot(a, a)); }
static gtl_vec3 vr_v3norm(gtl_vec3 a, gtl_vec3 fallback) {
    f32 l = vr_v3len(a);
    return (l > 1e-4f) ? vr_v3mul(a, 1.0f / l) : fallback;
}

// Interpolation tags for the arm and hand boxes (one per box slot), so RT64 moves them smoothly
// between game frames at the headset's rate instead of in 30 Hz steps.
#define VR_MTXTAG_HANDS 0x7E0000

// One box: centre c, half-extent axes ax, ay, az (right-handed, any lengths). slot: a stable
// number per box, for its interpolation tag.
static Gfx *vr_draw_box(Gfx *dl, s32 buf, s32 *n, s32 slot, u32 rgba, s32 bright, gtl_vec3 c, gtl_vec3 ax, gtl_vec3 ay, gtl_vec3 az) {
    f32 m[4][4];
    Vtx *v;
    Mtx *mx;
    if (*n >= VR_BOXES) {
        return dl;
    }
    v = vr_box_vtx[buf][*n];
    mx = &vr_box_mtx[buf][*n];
    (*n)++;
    vr_hand_box(v, rgba, bright);   // a cube of +-VR_HAND_UNIT, y squashed by 0.8
    m[0][0] = ax.x / VR_HAND_UNIT; m[0][1] = ax.y / VR_HAND_UNIT; m[0][2] = ax.z / VR_HAND_UNIT; m[0][3] = 0.0f;
    m[1][0] = ay.x / (0.8f * VR_HAND_UNIT); m[1][1] = ay.y / (0.8f * VR_HAND_UNIT); m[1][2] = ay.z / (0.8f * VR_HAND_UNIT); m[1][3] = 0.0f;
    m[2][0] = az.x / VR_HAND_UNIT; m[2][1] = az.y / VR_HAND_UNIT; m[2][2] = az.z / VR_HAND_UNIT; m[2][3] = 0.0f;
    m[3][0] = c.x; m[3][1] = c.y; m[3][2] = c.z; m[3][3] = 1.0f;
    guMtxF2L(m, mx);
    if (skip_interpolation) {
        gEXMatrixGroupSkipAll(dl++, VR_MTXTAG_HANDS + slot, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    } else {
        gEXMatrixGroupSimpleNormal(dl++, VR_MTXTAG_HANDS + slot, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    }
    gSPMatrix(dl++, osVirtualToPhysical(mx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(dl++, osVirtualToPhysical(v), 8, 0);
    gSP2Triangles(dl++, 0, 2, 1, 0, 0, 3, 2, 0);
    gSP2Triangles(dl++, 4, 5, 6, 0, 4, 6, 7, 0);
    gSP2Triangles(dl++, 0, 1, 5, 0, 0, 5, 4, 0);
    gSP2Triangles(dl++, 3, 7, 6, 0, 3, 6, 2, 0);
    gSP2Triangles(dl++, 0, 4, 7, 0, 0, 7, 3, 0);
    gSP2Triangles(dl++, 1, 2, 6, 0, 1, 6, 5, 0);
    gEXPopMatrixGroup(dl++, G_MTX_MODELVIEW);
    return dl;
}

// A limb from a to b, thickness t (half), with `side` as its sideways axis.
static Gfx *vr_draw_limb(Gfx *dl, s32 buf, s32 *n, s32 slot, u32 rgba, gtl_vec3 a, gtl_vec3 b, f32 t, gtl_vec3 side) {
    gtl_vec3 d = vr_v3sub(b, a);
    f32 len = vr_v3len(d);
    gtl_vec3 z, x, y;
    if (len < 0.2f) {
        return dl;
    }
    z = vr_v3mul(d, 1.0f / len);
    x = vr_v3norm(vr_v3sub(side, vr_v3mul(z, vr_v3dot(side, z))), vr_v3(1.0f, 0.0f, 0.0f));
    y = vr_v3cross(z, x);
    return vr_draw_box(dl, buf, n, slot, rgba, 0, vr_v3mul(vr_v3add(a, b), 0.5f), vr_v3mul(x, t), vr_v3mul(y, t), vr_v3mul(z, len * 0.5f + t * 0.3f));
}

// Gorilla Tag-style arms: shoulder, elbow (bent out and down, two-bone reach), hand. The hand is a
// palm with fingers and a thumb, turned with the controller's aim; it brightens on a surface.
Gfx *vr_gorilla_draw_hands(Gfx *dl) {
    CharacterChange *cc;
    s32 buf, h, k, n = 0;
    f32 ws = vr_ws, hr = vr_params.hand_radius;
    f32 yaw = vr_yaw + vr_hands.head_yaw;
    gtl_vec3 right, fwd, up = vr_v3(0.0f, 1.0f, 0.0f);

    if (!vr_active || character_change_array == NULL) {
        return dl;
    }
    cc = &character_change_array[0];
    buf = D_global_asm_807444FC;
    k = current_character_index[0];
    if (k < 0 || k > 4) {
        k = 0;
    }
    // the head's facing, flat (game convention: forward = (sin, cos), right = (-cos, sin))
    fwd = vr_v3(vr_sin(yaw), 0.0f, vr_cos(yaw));
    right = vr_v3(-vr_cos(yaw), 0.0f, vr_sin(yaw));

    gDPPipeSync(dl++);
    gDPSetCycleType(dl++, G_CYC_1CYCLE);
    gDPSetRenderMode(dl++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(dl++, G_CC_SHADE, G_CC_SHADE);
    gSPLoadGeometryMode(dl++, 0);
    gSPSetGeometryMode(dl++, G_SHADE | G_ZBUFFER | G_CULL_BACK);
    if (skip_interpolation || skip_persp_interp) {
        gEXMatrixGroupSkipAllAspect(dl++, MTXTAG_CAMERAPROJECTION, G_EX_NOPUSH, G_MTX_PROJECTION, G_EX_EDIT_NONE, G_EX_ASPECT_AUTO);
    } else {
        gEXMatrixGroupSimpleNormal(dl++, MTXTAG_CAMERAPROJECTION, G_EX_NOPUSH, G_MTX_PROJECTION, G_EX_EDIT_NONE);
    }
    gSPMatrix(dl++, osVirtualToPhysical(&cc->unk88[buf]), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(dl++, osVirtualToPhysical(&cc->unk8[buf]), G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);

    for (h = 0; h < 2; h++) {
        f32 side = (h == 0) ? -1.0f : 1.0f;
        gtl_vec3 head = vr_v3add(vr_loco.body, vr_v3(0.0f, vr_params.head_height + vr_loco.last_head.y, 0.0f));
        gtl_vec3 sh = vr_v3add(vr_v3add(head, vr_v3mul(right, side * 0.18f * ws)), vr_v3(0.0f, -0.24f * ws, 0.0f));
        gtl_vec3 hand = vr_loco.hand[h].pos;
        if (!vr_loco.hand[h].touching) {
            // a free hand is drawn a little ahead along its motion: it reaches the eyes a frame
            // or so after it was sampled (held hands stay exactly where they hold)
            gtl_vec3 v = vr_play_to_world(vr_hands.hand_vel[h], vr_ws * vr_hands.arm_scale);
            hand = vr_v3add(hand, vr_v3mul(v, 0.045f));
        }
        gtl_vec3 d = vr_v3sub(hand, sh);
        f32 dist = vr_v3len(d);
        f32 seg = 0.36f * ws * vr_hands.arm_scale;   // upper arm = forearm
        gtl_vec3 dir = vr_v3norm(d, vr_v3mul(up, -1.0f));
        gtl_vec3 bend, elbow, aim, palm_up, palm_side;
        f32 reach, out;
        u32 fur = vr_arm_colours[k], skin = vr_skin_colours[k];
        s32 on = vr_loco.hand[h].touching;

        // elbow: halfway, pushed out sideways and down by what the two bones have left over
        reach = dist * 0.5f;
        out = (reach < seg) ? vr_sqrt(seg * seg - reach * reach) : 0.0f;
        bend = vr_v3add(vr_v3mul(right, side), vr_v3mul(up, -0.6f));
        bend = vr_v3norm(vr_v3sub(bend, vr_v3mul(dir, vr_v3dot(bend, dir))), right);
        elbow = vr_v3add(vr_v3add(sh, vr_v3mul(d, 0.5f)), vr_v3mul(bend, out));

        dl = vr_draw_limb(dl, buf, &n, h * 5 + 0, fur, sh, elbow, hr * 0.95f, right);
        dl = vr_draw_limb(dl, buf, &n, h * 5 + 1, fur, elbow, hand, hr * 0.8f, right);

        // the hand: forward along the controller's aim, palm down-ish
        aim = vr_v3norm(vr_play_to_world(vr_hands.hand_dir[h], 1.0f), fwd);
        palm_side = vr_v3norm(vr_v3cross(up, aim), vr_v3mul(right, -1.0f));   // the hand's left
        palm_up = vr_v3cross(aim, palm_side);
        // palm
        dl = vr_draw_box(dl, buf, &n, h * 5 + 2, skin, on, hand, vr_v3mul(palm_side, hr * 1.0f), vr_v3mul(palm_up, hr * 0.45f),
            vr_v3mul(aim, hr * 0.9f));
        // fingers, curled forward and a little down
        dl = vr_draw_box(dl, buf, &n, h * 5 + 3, skin, on,
            vr_v3add(vr_v3add(hand, vr_v3mul(aim, hr * 1.55f)), vr_v3mul(palm_up, -hr * 0.25f)),
            vr_v3mul(palm_side, hr * 0.95f), vr_v3mul(palm_up, hr * 0.35f), vr_v3mul(aim, hr * 0.6f));
        // thumb, on the inner side (palm_side points to the hand's left)
        dl = vr_draw_box(dl, buf, &n, h * 5 + 4, skin, on,
            vr_v3add(vr_v3add(hand, vr_v3mul(palm_side, side * hr * 1.15f)), vr_v3mul(aim, hr * 0.5f)),
            vr_v3mul(palm_side, hr * 0.3f), vr_v3mul(palm_up, hr * 0.3f), vr_v3mul(aim, hr * 0.55f));
    }
    gDPPipeSync(dl++);
    return dl;
}

s32 vr_gorilla_hide_cutscene_bars(void) {
    return recomp_vr_is_active() && vr_hands.hide_cutscene_bars;
}
