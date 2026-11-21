#include "game/game.h"
#include <cmath>

void Game::updateCamera(float frameSeconds) {
    int viewW = SCREEN_W - sidebarW;
    const float elapsed=frameSeconds<0 ? GetFrameTime() : frameSeconds;
    // Camera speed is measured in pixels per second, independent of simulation
    // speed. Bound a stalled frame so returning to the window cannot fling it.
    const float dt=std::isfinite(elapsed)?std::clamp(elapsed,0.f,0.1f):0.f;
    float sp = camSpeed * 60.f * dt / camZoom;
    if (kDown(KEY_LEFT)) camX -= sp;
    if (kDown(KEY_RIGHT)) camX += sp;
    if (kDown(KEY_UP)) camY -= sp;
    if (kDown(KEY_DOWN)) camY += sp;
    Vector2 m = mousePos();
    // 开局短时禁止边缘卷轴：从菜单点「开始」后鼠标常在侧栏/边缘，否则镜头瞬间被拖离基地车
    if (camEdgeLock > 0) {
        camEdgeLock--;
    } else if((sim.active || IsWindowFocused()) && m.x>=0 && m.y>=0 && m.x<SCREEN_W && m.y<SCREEN_H) {
        if (m.x < 4) camX -= sp;
        if (m.x > SCREEN_W - 4) camX += sp;
        if (m.y < 4) camY -= sp;
        if (m.y > SCREEN_H - 4) camY += sp;
    }

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

    clampCameraToMap();
}

void Game::clampCameraToMap() {
    // RA2：视野不能拖出地图外；视口卡在等距地图 AABB 内（与 bakeTerrain 世界域一致）
    if (world.map.w <= 0 || world.map.h <= 0) return;
    int viewW = SCREEN_W - sidebarW;
    float visW = (float)viewW / camZoom;
    float visH = (float)SCREEN_H / camZoom;

    float mapL, mapR, mapT, mapB;
    if (terrainW > 0 && terrainH > 0) {
        mapL = -terrainOX;
        mapR = -terrainOX + (float)terrainW;
        mapT = 0.0f;
        mapB = (float)terrainH;
    } else {
        const int mw = world.map.w, mh = world.map.h;
        mapL = -(float)(mh - 1) * (TILE_W / 2.0f);
        mapR = (float)(mw - 1) * (TILE_W / 2.0f) + (float)TILE_W;
        mapT = 0.0f;
        mapB = (float)(mw + mh - 2) * (TILE_H / 2.0f) + (float)TILE_H;
    }

    const float mapW = mapR - mapL;
    const float mapH = mapB - mapT;
    float minX, maxX, minY, maxY;
    if (mapW <= visW) {
        minX = maxX = mapL - (visW - mapW) * 0.5f; // 地图比视口窄：居中
    } else {
        minX = mapL;
        maxX = mapR - visW;
    }
    if (mapH <= visH) {
        minY = maxY = mapT - (visH - mapH) * 0.5f;
    } else {
        minY = mapT;
        maxY = mapB - visH;
    }
    camX = std::clamp(camX, minX, maxX);
    camY = std::clamp(camY, minY, maxY);
}

