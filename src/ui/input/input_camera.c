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

    if (!gs->build_mode.active && !ui->build_panel_open) {
        if (mp.x < CAM_EDGE)              cam->target.x -= speed * dt;
        if (mp.x > SCREEN_W - CAM_EDGE)   cam->target.x += speed * dt;
        if (mp.y < CAM_EDGE)              cam->target.y -= speed * dt;
        if (mp.y > SCREEN_H - CAM_EDGE)   cam->target.y += speed * dt;
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        Vector2 before = GetScreenToWorld2D(mp, *cam);
        cam->zoom += wheel * ZOOM_SPEED * cam->zoom;
        cam->zoom = clampf(cam->zoom, ZOOM_MIN, ZOOM_MAX);
        Vector2 after = GetScreenToWorld2D(mp, *cam);
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
