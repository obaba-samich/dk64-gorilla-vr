/* A tiny world for the locomotion tests: an optional floor plane plus axis-aligned boxes.
 * Boxes are inflated by the sphere radius with square corners, close enough for tests. */
#ifndef FAKE_WORLD_H
#define FAKE_WORLD_H

#include <math.h>
#include "../gtl.h"

#define FW_MAX_BOXES 16

typedef struct fw_box {
    gtl_vec3 lo, hi;
    float slip;
    int surface;
} fw_box;

typedef struct fake_world {
    int has_floor;
    float floor_y;
    float floor_slip;
    fw_box boxes[FW_MAX_BOXES];
    int box_count;
} fake_world;

static float fw_sqrt(float v) { return sqrtf(v); }

static int fw_sweep_floor(const fake_world *fw, gtl_vec3 a, gtl_vec3 b, float r, gtl_hit *hit) {
    float da = a.y - r - fw->floor_y;
    float db = b.y - r - fw->floor_y;
    if (b.y >= a.y) {
        return 0; /* moving up or level never hits a floor */
    }
    if (da < 0.0f) {
        hit->t = 0.0f; /* started inside and still going down */
    } else if (db < 0.0f) {
        hit->t = da / (da - db);
    } else {
        return 0;
    }
    hit->normal.x = 0; hit->normal.y = 1; hit->normal.z = 0;
    hit->slip = fw->floor_slip;
    hit->surface = 1;
    return 1;
}

static int fw_sweep_box(const fw_box *bx, gtl_vec3 a, gtl_vec3 b, float r, gtl_hit *hit) {
    float lo[3] = { bx->lo.x - r, bx->lo.y - r, bx->lo.z - r };
    float hi[3] = { bx->hi.x + r, bx->hi.y + r, bx->hi.z + r };
    float s[3] = { a.x, a.y, a.z };
    float d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
    float tmin = 0.0f, tmax = 1.0f;
    int axis = -1, sign = 0, i;
    int inside = 1;

    for (i = 0; i < 3; i++) {
        if (s[i] < lo[i] || s[i] > hi[i]) {
            inside = 0;
        }
    }
    if (inside) {
        /* push out through the nearest face, only if moving further in */
        float best = 1e30f;
        for (i = 0; i < 3; i++) {
            float dl = s[i] - lo[i], dh = hi[i] - s[i];
            if (dl < best) { best = dl; axis = i; sign = -1; }
            if (dh < best) { best = dh; axis = i; sign = 1; }
        }
        if (d[axis] * (float)sign >= 0.0f) {
            return 0;
        }
        tmin = 0.0f;
    } else {
        for (i = 0; i < 3; i++) {
            if (fabsf(d[i]) < 1e-9f) {
                if (s[i] < lo[i] || s[i] > hi[i]) {
                    return 0;
                }
                continue;
            }
            {
                float t1 = (lo[i] - s[i]) / d[i];
                float t2 = (hi[i] - s[i]) / d[i];
                int sg = -1;
                if (t1 > t2) { float tt = t1; t1 = t2; t2 = tt; sg = 1; }
                if (t1 > tmin) { tmin = t1; axis = i; sign = sg; }
                if (t2 < tmax) { tmax = t2; }
                if (tmin > tmax) {
                    return 0;
                }
            }
        }
        if (axis < 0) {
            return 0;
        }
    }
    hit->t = tmin;
    hit->normal.x = axis == 0 ? (float)sign : 0.0f;
    hit->normal.y = axis == 1 ? (float)sign : 0.0f;
    hit->normal.z = axis == 2 ? (float)sign : 0.0f;
    hit->slip = bx->slip;
    hit->surface = bx->surface;
    return 1;
}

static int fw_sweep(void *user, gtl_vec3 a, gtl_vec3 b, float r, gtl_hit *out) {
    const fake_world *fw = (const fake_world *)user;
    gtl_hit best, h;
    int found = 0, i;
    best.t = 2.0f;
    if (fw->has_floor && fw_sweep_floor(fw, a, b, r, &h) && h.t < best.t) {
        best = h;
        found = 1;
    }
    for (i = 0; i < fw->box_count; i++) {
        if (fw_sweep_box(&fw->boxes[i], a, b, r, &h) && h.t < best.t) {
            best = h;
            found = 1;
        }
    }
    if (found) {
        *out = best;
    }
    return found;
}

static gtl_world fw_world(fake_world *fw) {
    gtl_world w;
    w.user = fw;
    w.sweep = fw_sweep;
    w.sqrt_fn = fw_sqrt;
    return w;
}

static void fw_add_box(fake_world *fw, float x0, float y0, float z0, float x1, float y1, float z1,
                       float slip) {
    fw_box *b = &fw->boxes[fw->box_count++];
    b->lo.x = x0; b->lo.y = y0; b->lo.z = z0;
    b->hi.x = x1; b->hi.y = y1; b->hi.z = z1;
    b->slip = slip;
    b->surface = 100 + fw->box_count;
}

#endif
