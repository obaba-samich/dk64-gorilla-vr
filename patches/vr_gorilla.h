// Arm locomotion, game side. Mirrors src/vr/include/dk64vr_gorilla.h word for word.
#ifndef __VR_GORILLA_H__
#define __VR_GORILLA_H__

#include "patch_helpers.h"

#ifdef MIPS
typedef struct VrHands {
    u32 seq;
    s32 valid;
    f32 head[3];
    f32 head_yaw;
    f32 hand[2][3];
    f32 hand_dir[2][3];
    f32 hand_vel[2][3];
    f32 grip[2];
    f32 trigger[2];
    u32 buttons;
    f32 stick[2][2];
    f32 world_scale;
    f32 arm_scale;
    f32 jump_multiplier;
    f32 max_jump_speed;
    s32 gorilla;
    s32 stick_move;
    s32 gestures;
    s32 dominant;
    s32 duck_crouch;
    s32 tag_anywhere;
    s32 diag_tag;
    s32 warp_req;
    s32 warp_map;
    s32 warp_exit;
    s32 unlock_req;
    f32 floor_y;
    s32 match_height;
} VrHands;

typedef struct VrBody {
    u32 frame;
    s32 active;
    f32 head[3];
    f32 yaw;
    f32 hand[2][3];
    s32 touching[2];
    f32 speed;
    f32 launch_speed;
    s32 launch_count;
    s32 haptic[2];
    u32 press;
    s32 stick_valid;
    f32 stick[2];
    s32 gun_out;
    s32 stick_free;
    s32 mode;
    f32 world_scale;
} VrBody;

#define VR_BODY_OFF 0
#define VR_BODY_ARMS 1
#define VR_BODY_THIRD_PERSON 2
#define VR_BODY_BARREL 3
#define VR_BODY_CLIMB 4

#define VR_BTN_A (1 << 0)
#define VR_BTN_B (1 << 1)
#define VR_BTN_X (1 << 2)
#define VR_BTN_Y (1 << 3)
#endif

DECLARE_FUNC(void, recomp_vr_get_hands, struct VrHands*);
DECLARE_FUNC(void, recomp_vr_publish_body, const struct VrBody*);

#ifdef MIPS
// Once per game frame, before the actors update (boot_logos_patches.c).
void vr_gorilla_pre_frame(void);
// Once per game frame, after the frame is built (vr_frame_hook).
void vr_gorilla_post_frame(void);
// Draws the hands; called right after the world in the frame's display list.
Gfx *vr_gorilla_draw_hands(Gfx *dl);
#endif

#endif
