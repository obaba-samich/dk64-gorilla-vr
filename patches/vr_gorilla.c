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
        // the floor top here is well above the sphere's bottom: we went into a side
        f32 dx = came_from.x - p.x, dz = came_from.z - p.z;
        f32 l = vr_sqrt(dx * dx + dz * dz);
        if (l < 1e-4f) {
            return 0; // straight down into a step's side can't happen; ignore
        }
        hit->normal.x = dx / l;
        hit->normal.y = 0.0f;
        hit->normal.z = dz / l;
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
static f32 vr_height;              // head above the feet, world units (per Kong)
static s32 vr_initialised;
static Actor *vr_player_seen;
static u32 vr_frame;

// For the movement patch: what to add to the player this frame.
static s32 vr_drive;               // 1 = replace the game's move with vr_disp
static gtl_vec3 vr_disp;

// Pending throw, waiting for the game to start the jump.
static s32 vr_launch_pending;
static gtl_vec3 vr_launch_v;
static s32 vr_launch_count;

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
#define VR_STATE_NORMAL 0x0C

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
    vr_height = kb->height * scale;
    vr_params.head_height = vr_height - vr_params.body_radius;
    vr_params.arm_length = 0.9f * vr_hands.world_scale * vr_hands.arm_scale * kb->arm;
    vr_params.jump_multiplier = vr_hands.jump_multiplier * kb->launch;
    vr_params.max_jump_speed = vr_hands.max_jump_speed * vr_hands.world_scale;
    vr_params.min_launch_speed = 0.8f * vr_hands.world_scale;
    vr_params.unstick_distance = 0.2f * vr_hands.world_scale;
    vr_params.gravity = 0.0f;  // the game's own
    vr_sweep_ctx.player = player;
    vr_sweep_ctx.wall_window = 1.0f * vr_hands.world_scale;
}

static void vr_build_input(gtl_input *in, f32 dt) {
    f32 k = vr_hands.world_scale * vr_hands.arm_scale;
    s32 h;
    in->head = vr_play_to_world(vr_hands.head, vr_hands.world_scale);
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
    if (!vr_hands.gestures) {
        gst_reset(&vr_gst);
        return;
    }
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
    g.dt = dt;
    gst_step_frame(&vr_gst, &vr_gst_params, &g, &vr_gst_out);
    if (vr_body_mode != VR_BODY_CLIMB) {
        // hand-over-hand only means "climb" on something climbable; elsewhere it would walk
        vr_gst_out.stick_valid = 0;
        vr_gst_out.events &= ~GST_EV_CLIMB;
    }
    if (vr_gst_out.events & ~(GST_EV_CROUCH | GST_EV_CLIMB | GST_EV_TWIRL)) {
        vr_tag_buzz = 1;  // a short buzz on both hands: the gesture registered
        recomp_printf("[vr-gesture] events %x hold %x\n", vr_gst_out.events, vr_gst_out.hold);
    }
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
    vr_gst_out.hold = 0;
    vr_gst_out.stick_valid = 0;
    if (!vr_should_drive(player)) {
        vr_initialised = 0;
        vr_air_frames = 0;
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
        } else {
            vr_loco.body = before;
        }
    }

    // Under water? The floor query also reports the water surface over that spot.
    {
        f32 floor_y, water_y = 0.0f;
        f32 head_y = vr_loco.body.y + vr_params.head_height;
        vr_floor_at(player, vr_loco.body.x, vr_loco.body.z, head_y + 2000.0f, &floor_y);
        s32 has_water = func_global_asm_8066715C(&water_y);
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

    if (vr_body_mode == VR_BODY_CLIMB) {
        // on a tree, ladder or vine the game holds the player; the hands only climb (stick)
        in.hand_enabled[0] = in.hand_enabled[1] = 0;
        in.mode = GTL_MODE_FROZEN;
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
        || vr_in_water || (vr_disp.x * vr_disp.x + vr_disp.z * vr_disp.z) > 0.0001f);
    vr_active = 1;

    if (ev.launched) {
        f32 sp = vr_sqrt(ev.launch_velocity.x * ev.launch_velocity.x + ev.launch_velocity.y * ev.launch_velocity.y
            + ev.launch_velocity.z * ev.launch_velocity.z);
        vr_launch_v = ev.launch_velocity;
        if (ev.launch_velocity.y > 0.35f * sp) {
            // thrown upwards: let the game start a real jump (A), then swap in our speed
            vr_launch_pending = 6;  // frames to wait for the game's jump to start
            vr_launch_count++;
        } else {
            // thrown along the ground: the game's own run speed carries it and slows it down
            f32 scale_y = (player->animation_state != NULL) ? player->animation_state->scale_y : 0.15f;
            f32 per_frame = 6.0f / (scale_y * 30.0f);
            f32 vh = vr_sqrt(vr_launch_v.x * vr_launch_v.x + vr_launch_v.z * vr_launch_v.z);
            player->unkB8 = vh * per_frame;
            if (vh > 0.01f) {
                player->unkEE = vr_heading_to_game(vr_atan2(vr_launch_v.x, vr_launch_v.z));
            }
        }
        vr_body.launch_speed = vr_sqrt(ev.launch_velocity.x * ev.launch_velocity.x
            + ev.launch_velocity.y * ev.launch_velocity.y + ev.launch_velocity.z * ev.launch_velocity.z);
        recomp_printf("[vr-loco] launch %.1f %.1f %.1f\n", ev.launch_velocity.x, ev.launch_velocity.y,
            ev.launch_velocity.z);
    }
    if (vr_launch_pending > 0) {
        vr_launch_pending--;
        if (player->y_velocity > 0.0f) {
            // the game has started a jump: give it the throw's speed instead
            f32 scale_y = (player->animation_state != NULL) ? player->animation_state->scale_y : 0.15f;
            f32 per_frame = 6.0f / (scale_y * 30.0f);
            f32 vh = vr_sqrt(vr_launch_v.x * vr_launch_v.x + vr_launch_v.z * vr_launch_v.z);
            player->y_velocity = vr_launch_v.y * per_frame;
            player->unkB8 = vh * per_frame;
            if (vh > 0.01f) {
                player->unkEE = vr_heading_to_game(vr_atan2(vr_launch_v.x, vr_launch_v.z));
                player->y_rotation = player->unkEE;
            }
            vr_launch_pending = 0;
            recomp_printf("[vr-loco] jump started, speed set to %.2f up %.2f along\n", player->y_velocity, player->unkB8);
        }
    }
    vr_gorilla_gestures(player, &in, dt);
    if (!(VR_ACTOR_CONTACT(player) & 1) && !vr_loco.hand[0].touching && !vr_loco.hand[1].touching && !vr_in_water) {
        vr_air_frames++;
    } else {
        vr_air_frames = 0;
    }
    if (vr_air_frames > 0 && player->y_velocity > -0.01f && player->y_velocity < 0.01f && !vr_launch_pending) {
        vr_hang_frames++;
    } else {
        vr_hang_frames = 0;
    }
    if ((vr_frame % 60) == 0) {
        recomp_printf("[vr-loco] at %.1f %.1f %.1f state %d vy %.2f holding %d%d\n", player->x_position,
            player->y_position, player->z_position, player->control_state, player->y_velocity,
            vr_loco.hand[0].touching, vr_loco.hand[1].touching);
    }
    {
        s32 h;
        for (h = 0; h < 2; h++) {
            vr_body.haptic[h] = 0;
            if (vr_tag_buzz) {
                vr_body.haptic[h] = 60;
            }
            if (ev.hand_contact[h]) {
                f32 strength = ev.contact_speed[h] / (2.0f * vr_hands.world_scale);
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
    if (vr_active && character_change_array != NULL && character_change_array[0].playerPointer != NULL) {
        player = character_change_array[0].playerPointer;
        // face where the head looks, so guns and moves go that way
        if (vr_body_mode == VR_BODY_ARMS && !vr_launch_pending && player->y_velocity <= 0.0f) {
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
            gtl_vec3 off = vr_play_to_world(vr_hands.head, vr_hands.world_scale);
            vr_body.head[0] = player->x_position - off.x;
            vr_body.head[1] = player->y_position + vr_height - off.y;
            vr_body.head[2] = player->z_position - off.z;
        }
        vr_body.yaw = vr_yaw;
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

    if (vr_active && vr_hide_body && character_change_array != NULL
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

static Vtx vr_hand_vtx[2][2][8];
static Mtx vr_hand_mtx[2][2];

static const u32 vr_kong_colours[5] = {
    0xF0C820FF, // DK yellow
    0xE03028FF, // Diddy red
    0x3060E0FF, // Lanky blue
    0x9040D0FF, // Tiny purple
    0x30B040FF, // Chunky green
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

Gfx *vr_gorilla_draw_hands(Gfx *dl) {
    CharacterChange *cc;
    s32 buf, h, k;
    f32 size;

    if (!vr_active || character_change_array == NULL) {
        return dl;
    }
    cc = &character_change_array[0];
    buf = D_global_asm_807444FC;
    k = current_character_index[0];
    if (k < 0 || k > 4) {
        k = 0;
    }
    size = vr_params.hand_radius * 1.1f;

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
        f32 m[4][4];
        f32 s = size / VR_HAND_UNIT;
        Vtx *v = vr_hand_vtx[buf][h];
        vr_hand_box(v, vr_kong_colours[k], vr_loco.hand[h].touching);
        guScaleF(m, s, s, s);
        m[3][0] = vr_loco.hand[h].pos.x;
        m[3][1] = vr_loco.hand[h].pos.y;
        m[3][2] = vr_loco.hand[h].pos.z;
        guMtxF2L(m, &vr_hand_mtx[buf][h]);
        gSPMatrix(dl++, osVirtualToPhysical(&vr_hand_mtx[buf][h]), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(dl++, osVirtualToPhysical(v), 8, 0);
        // six faces, outward winding
        gSP2Triangles(dl++, 0, 2, 1, 0, 0, 3, 2, 0);   // -z
        gSP2Triangles(dl++, 4, 5, 6, 0, 4, 6, 7, 0);   // +z
        gSP2Triangles(dl++, 0, 1, 5, 0, 0, 5, 4, 0);   // -y
        gSP2Triangles(dl++, 3, 7, 6, 0, 3, 6, 2, 0);   // +y
        gSP2Triangles(dl++, 0, 4, 7, 0, 0, 7, 3, 0);   // -x
        gSP2Triangles(dl++, 1, 2, 6, 0, 1, 6, 5, 0);   // +x
    }
    gDPPipeSync(dl++);
    return dl;
}
