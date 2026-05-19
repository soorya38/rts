/*=============================================================
 * input_camera.c  –  Camera panning and zooming
 *
 * Handles keyboard, mouse-edge, right-click drag, mouse wheel,
 * and touch/pinch-to-zoom camera control.  The camera target is
 * clamped to keep the map visible.
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>

/* ── Tuning constants ──────────────────────────────────────── */

static const float CAMERA_PAN_SPEED      = 280.0f;   /* pixels/sec at 1× zoom */
static const int   EDGE_PAN_MARGIN       = 12;       /* pixels from screen edge */
static const float MOUSE_ZOOM_SPEED      = 0.12f;    /* zoom per scroll notch */
static const float ZOOM_MIN              = 0.8f;
static const float ZOOM_MAX              = 2.8f;
static const float TOUCH_PINCH_SCALE     = 0.01f;    /* pinch-distance → zoom */

/* Isometric camera bounds (generous padding around map edges). */
static const float CAM_PADDING_TOP       = -30.0f;
static const float CAM_PADDING_BOTTOM    = 100.0f;

/* ── Implementation ────────────────────────────────────────── */

void update_camera(GameState *gs, UIState *ui, float dt)
{
    Camera2D *cam = &ui->camera;
    Vector2 mouse_pos = GetMousePosition();
    float speed = CAMERA_PAN_SPEED / cam->zoom;

    /* ── Keyboard panning ──────────────────────────────────── */
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    cam->target.y -= speed * dt;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))   cam->target.y += speed * dt;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))   cam->target.x -= speed * dt;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))  cam->target.x += speed * dt;

    /* ── Platform-specific panning ─────────────────────────── */
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    /* Single-finger swipe panning on mobile. */
    if (GetTouchPointCount() == 1 && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        Vector2 delta = GetMouseDelta();
        cam->target.x -= delta.x / cam->zoom;
        cam->target.y -= delta.y / cam->zoom;
    }
#else
    /* Right-click drag panning on desktop. */
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();
        cam->target.x -= delta.x / cam->zoom;
        cam->target.y -= delta.y / cam->zoom;
    }

    /* Edge-of-screen panning (disabled during build placement). */
    if (!gs->build_mode.active && !ui->build_panel_open) {
        int screen_w = GetScreenWidth();
        int screen_h = GetScreenHeight();
        if (mouse_pos.x < EDGE_PAN_MARGIN)               cam->target.x -= speed * dt;
        if (mouse_pos.x > screen_w - EDGE_PAN_MARGIN)     cam->target.x += speed * dt;
        if (mouse_pos.y < EDGE_PAN_MARGIN)               cam->target.y -= speed * dt;
        if (mouse_pos.y > screen_h - EDGE_PAN_MARGIN)     cam->target.y += speed * dt;
    }
#endif

    /* ── Zoom (mouse wheel + touch pinch) ──────────────────── */
    float zoom_delta = 0.0f;
    Vector2 zoom_pivot = mouse_pos;

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        zoom_delta = wheel * MOUSE_ZOOM_SPEED;
    }

    /* Two-finger pinch-to-zoom: track distance between fingers
       each frame and convert the delta to a zoom change. */
    static float previous_pinch_distance = -1.0f;

    if (GetTouchPointCount() >= 2) {
        Vector2 touch_a = GetTouchPosition(0);
        Vector2 touch_b = GetTouchPosition(1);
        float dx = touch_a.x - touch_b.x;
        float dy = touch_a.y - touch_b.y;
        float current_dist = sqrtf(dx * dx + dy * dy);

        if (previous_pinch_distance < 0 || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            previous_pinch_distance = current_dist;
        } else {
            zoom_delta = (current_dist - previous_pinch_distance) * TOUCH_PINCH_SCALE;
            previous_pinch_distance = current_dist;
            zoom_pivot = (Vector2){
                (touch_a.x + touch_b.x) * 0.5f,
                (touch_a.y + touch_b.y) * 0.5f
            };
        }
    } else {
        previous_pinch_distance = -1.0f;
    }

    /* Apply zoom around the pivot point so the world position
       under the cursor/fingers stays fixed on screen. */
    if (zoom_delta != 0.0f) {
        Vector2 world_before = GetScreenToWorld2D(zoom_pivot, *cam);
        cam->zoom += zoom_delta * cam->zoom;
        cam->zoom = clampf(cam->zoom, ZOOM_MIN, ZOOM_MAX);
        Vector2 world_after = GetScreenToWorld2D(zoom_pivot, *cam);
        cam->target.x += (world_before.x - world_after.x);
        cam->target.y += (world_before.y - world_after.y);
    }

    /* ── Clamp camera to keep map visible ──────────────────── */
    float half_view_w = (GetScreenWidth()  * 0.5f) / cam->zoom;
    float half_view_h = (GetScreenHeight() * 0.5f) / cam->zoom;

    float world_min_x = -(float)(MAP_H * TILE_SIZE);
    float world_max_x =  (float)(MAP_W * TILE_SIZE);
    float world_min_y = CAM_PADDING_TOP;
    float world_max_y = (float)(MAP_W + MAP_H) * TILE_SIZE * 0.5f + CAM_PADDING_BOTTOM;

    float clamp_min_x = world_min_x + half_view_w;
    float clamp_max_x = world_max_x - half_view_w;
    if (clamp_max_x < clamp_min_x) {
        clamp_min_x = clamp_max_x = (clamp_min_x + clamp_max_x) * 0.5f;
    }
    cam->target.x = clampf(cam->target.x, clamp_min_x, clamp_max_x);

    float clamp_min_y = world_min_y + half_view_h;
    float clamp_max_y = world_max_y - half_view_h;
    if (clamp_max_y < clamp_min_y) {
        clamp_min_y = clamp_max_y = (clamp_min_y + clamp_max_y) * 0.5f;
    }
    cam->target.y = clampf(cam->target.y, clamp_min_y, clamp_max_y);
}
