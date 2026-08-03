#include "game/game.h"
#include <cmath>

void Game::updateCamera() {
    int viewW = SCREEN_W - sidebarW;
    float sp = camSpeed * gameSpeed / camZoom;
    if (kDown(KEY_LEFT)) camX -= sp;
    if (kDown(KEY_RIGHT)) camX += sp;
    if (kDown(KEY_UP)) camY -= sp;
    if (kDown(KEY_DOWN)) camY += sp;
    Vector2 m = mousePos();
    if (m.x < 4) camX -= sp;
    if (m.x > SCREEN_W - 4) camX += sp;
    if (m.y < 4) camY -= sp;
    if (m.y > SCREEN_H - 4) camY += sp;

    // 滚轮缩放：以鼠标下世界点为锚
    float wheel = GetMouseWheelMove();
    if (wheel != 0.f && m.x >= 0 && m.x < (float)viewW && m.y >= 0 && m.y < (float)(SCREEN_H - BOTTOM_BAR_H)) {
        float oldZ = camZoom;
        float factor = (wheel > 0.f) ? 1.12f : (1.f / 1.12f);
        float newZ = std::clamp(oldZ * factor, CAM_ZOOM_MIN, CAM_ZOOM_MAX);
        if (fabsf(newZ - oldZ) > 1e-4f) {
            camX += m.x / oldZ - m.x / newZ;
            camY += m.y / oldZ - m.y / newZ;
            camZoom = newZ;
        }
    }

    float visW = (float)viewW / camZoom;
    float visH = (float)SCREEN_H / camZoom;
    float minX = -(float)world.map.h * TILE_W / 2.0f - visW - 200.0f;
    float maxX = (float)world.map.w * TILE_W / 2.0f + 200.0f;
    float minY = -visH - 100.0f;
    float maxY = (float)(world.map.w + world.map.h) * TILE_H / 2.0f + 100.0f;
    camX = std::clamp(camX, minX, maxX);
    camY = std::clamp(camY, minY, maxY);
}

