// Arm locomotion: what the host hands the game each game frame (controllers) and what the game
// hands back (where the body and hands ended up). Both are plain 32-bit words so the patches can
// read and write them straight out of RDRAM. The MIPS side mirrors these in patches/vr_gorilla.h;
// src/game/vr_api.cpp static_asserts the sizes.
#ifndef DK64VR_GORILLA_H
#define DK64VR_GORILLA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Controller buttons, dk64vr_hands.buttons
enum {
    DK64VR_BTN_A = 1 << 0,          // right A
    DK64VR_BTN_B = 1 << 1,          // right B
    DK64VR_BTN_X = 1 << 2,          // left X
    DK64VR_BTN_Y = 1 << 3,          // left Y
    DK64VR_BTN_LSTICK = 1 << 4,
    DK64VR_BTN_RSTICK = 1 << 5,
    DK64VR_BTN_MENU = 1 << 6
};

// Host to game. Positions in metres in the recentred play space: x right, y up, z towards the
// back (OpenXR convention), origin where the head was at the last recentre.
typedef struct dk64vr_hands {
    uint32_t seq;            // bumps every display frame the host writes it
    int32_t valid;           // tracking is live and the session is focused
    float head[3];
    float head_yaw;          // radians, counter-clockwise from above, 0 = recentre forward
    float hand[2][3];        // 0 left, 1 right (aim pose position)
    float hand_dir[2][3];    // aim pose forward (unit)
    float hand_vel[2][3];    // metres per second
    float grip[2];
    float trigger[2];
    uint32_t buttons;
    float stick[2][2];       // [hand][x,y], y up
    float world_scale;       // game units per metre for arm locomotion
    float arm_scale;         // multiplier on real arm reach
    float jump_multiplier;
    float max_jump_speed;    // metres per second
    int32_t gorilla;         // arm locomotion on
    int32_t stick_move;      // comfort smooth locomotion on the left stick
    int32_t gestures;        // ability gestures on
    int32_t dominant;        // gun hand, 0 left 1 right
    int32_t duck_crouch;     // ducking holds Z
} dk64vr_hands;

// Game to host, once per game frame. Game units, world space.
typedef struct dk64vr_body {
    uint32_t frame;
    int32_t active;          // the game is driving a first-person body this frame
    float head[3];           // camera position
    float yaw;               // body heading, radians, game convention (0 = +z)
    float hand[2][3];        // where the hands are drawn (on surfaces when touching)
    int32_t touching[2];
    float speed;             // body speed, units per second (stats overlay)
    float launch_speed;      // last launch, units per second
    int32_t launch_count;    // bumps on every release that should become a jump
    int32_t haptic[2];       // 0 none, else strength in percent for this frame
    uint32_t press;          // N64 buttons the gestures hold until the next game frame
    int32_t stick_valid;     // the gestures drive the N64 stick (climbing)
    float stick[2];          // -1..1, y up
    int32_t gun_out;
    int32_t stick_free;      // long in the air (Rocketbarrel, glides): the left stick steers
    int32_t mode;            // DK64VR_BODY_* below, for the log and the HUD
} dk64vr_body;

enum {
    DK64VR_BODY_OFF = 0,          // not driving (menus, cutscenes, setting off)
    DK64VR_BODY_ARMS = 1,         // arm locomotion, first person
    DK64VR_BODY_THIRD_PERSON = 2, // vehicles, animal buddies, the game's own first person
    DK64VR_BODY_BARREL = 3        // inside a Tag Barrel: lean to turn it
};

void dk64vr_publish_hands(const dk64vr_hands *hands);
void dk64vr_get_hands(dk64vr_hands *out);
void dk64vr_publish_body(const dk64vr_body *body);
// Latest two body snapshots, for interpolation at display rate.
void dk64vr_get_bodies(dk64vr_body *previous, dk64vr_body *latest);

#ifdef __cplusplus
}
#endif

#endif
