/*=============================================================
 * input_camera.c  –  Camera pan / zoom
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>

#define CAM_SPEED 280.0f
#define CAM_EDGE  12
#define ZOOM_SPEED 0.12f
#define ZOOM_MIN 0.8f
#define ZOOM_MAX 2.8f

void update_camera(GameState *gs, UIState *ui, float dt) {
    Camera2D *cam = &ui->camera;
    Vector2 mp = GetMousePosition();
    float speed = CAM_SPEED / cam->zoom;

    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    cam->target.y -= speed * dt;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))   cam->target.y += speed * dt;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))   cam->target.x -= speed * dt;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))  cam->target.x += speed * dt;

    /* Touch drag panning */
    if (IsGestureDetected(GESTURE_DRAG)) {
        Vector2 delta = GetGestureDragVector();
        cam->target.x -= delta.x / cam->zoom;
        cam->target.y -= delta.y / cam->zoom;
    }

    if (!gs->build_mode.active && !ui->build_panel_open) {
        if (mp.x < CAM_EDGE)              cam->target.x -= speed * dt;
        if (mp.x > SCREEN_W - CAM_EDGE)   cam->target.x += speed * dt;
        if (mp.y < CAM_EDGE)              cam->target.y -= speed * dt;
        if (mp.y > SCREEN_H - CAM_EDGE)   cam->target.y += speed * dt;
    }

    float wheel = GetMouseWheelMove();
    float pinch = 0.0f;
    Vector2 pinch_pos = mp; // default pivot
    
    if (wheel != 0.0f) {
        pinch = wheel * ZOOM_SPEED;
    } else if (IsGestureDetected(GESTURE_PINCH_IN) || IsGestureDetected(GESTURE_PINCH_OUT)) {
        Vector2 pinch_vec = GetGesturePinchVector();
        float pinch_scale = 1.0f + (pinch_vec.y * 0.01f); // approximate scale based on distance
        pinch = pinch_scale - 1.0f;
        // GetTouchPosition(1) and average it for accurate pinch pivot
        Vector2 p2 = GetTouchPosition(1);
        if(p2.x != 0 || p2.y != 0) {
            pinch_pos.x = (pinch_pos.x + p2.x) / 2.0f;
            pinch_pos.y = (pinch_pos.y + p2.y) / 2.0f;
            // Actually simpler: Raylib's gesture might just trigger frame by frame. Let's just adjust zoom by scale difference.
            // For simplicity, pinch_angle / scale can be mapped. 
            // A safer approach: track pinch scale. But typical pinch just modifies zoom if it returns 1.05 or 0.95.
        }
    }
    
    // Raylib 5.0 GetGesturePinchVector() returns pinch delta. Let's use standard touch points if GESTURE_PINCH is on.
    // Instead of raw pinch ratio, let's just use simple scaling.
    static float last_dist = -1;
    if(GetTouchPointCount() >= 2) {
        Vector2 t1 = GetTouchPosition(0);
        Vector2 t2 = GetTouchPosition(1);
        float dist = sqrtf((t1.x - t2.x)*(t1.x - t2.x) + (t1.y - t2.y)*(t1.y - t2.y));
        if(last_dist < 0 || IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) last_dist = dist; // Reset
        else {
            pinch = (dist - last_dist) * 0.01f;
            last_dist = dist;
            pinch_pos = (Vector2){(t1.x + t2.x)*0.5f, (t1.y + t2.y)*0.5f};
        }
    } else {
        // hacky reset
        last_dist = -1; 
    }
    
    if (pinch != 0.0f) {
        Vector2 before = GetScreenToWorld2D(pinch_pos, *cam);
        cam->zoom += pinch * cam->zoom;
        cam->zoom = clampf(cam->zoom, ZOOM_MIN, ZOOM_MAX);
        Vector2 after = GetScreenToWorld2D(pinch_pos, *cam);
        cam->target.x += (before.x - after.x);
        cam->target.y += (before.y - after.y);
    }

    float hw = (SCREEN_W * 0.5f) / cam->zoom, hh = (SCREEN_H * 0.5f) / cam->zoom;
    float min_cam_x = -(float)(MAP_H * TILE_SIZE), max_cam_x = (float)(MAP_W * TILE_SIZE);
    float min_cam_y = -30.0f, max_cam_y = (float)(MAP_W + MAP_H) * TILE_SIZE * 0.5f + 100.0f;
    float min_x = min_cam_x + hw, max_x = max_cam_x - hw;
    if (max_x < min_x) { float mid = (min_x + max_x) * 0.5f; min_x = max_x = mid; }
    cam->target.x = clampf(cam->target.x, min_x, max_x);
    float min_y = min_cam_y + hh, max_y = max_cam_y - hh;
    if (max_y < min_y) { float mid = (min_y + max_y) * 0.5f; min_y = max_y = mid; }
    cam->target.y = clampf(cam->target.y, min_y, max_y);
}
