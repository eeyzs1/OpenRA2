#include "game/game.h"

Rectangle Game::letterboxDst() {
    // 与 BeginDrawing 后 DrawTexturePro 使用同一套尺寸（HEAD：只用 GetRender*）。
    float rw = (float)GetRenderWidth(), rh = (float)GetRenderHeight();
    if (rw < 1.f || rh < 1.f) {
        rw = (float)GetScreenWidth();
        rh = (float)GetScreenHeight();
    }
    Rectangle dst{(rw - rh * SCREEN_W / SCREEN_H) / 2, 0, rh * SCREEN_W / SCREEN_H, rh};
    if (rw / SCREEN_W < rh / SCREEN_H)
        dst = Rectangle{0, (rh - rw * SCREEN_H / SCREEN_W) / 2, rw, rw * SCREEN_H / SCREEN_W};
    return dst;
}

Vector2 Game::mousePos() const {
    if (sim.active) return sim.pos;
    // 与 HEAD 一致：GetMousePosition 按 GetRender* letterbox 映到画布。
    // 勿再做 screen→render 预缩放（在部分环境下会把命中整体推向左上，
    // 表现为视觉单位落在逻辑框选位置的右下）。
    float rw = (float)GetRenderWidth(), rh = (float)GetRenderHeight();
    Vector2 m = GetMousePosition();
    if (rw <= 0.f || rh <= 0.f) return m;
    Rectangle dst = letterboxDst();
    if (dst.width < 1.f || dst.height < 1.f) return m;
    return {(m.x - dst.x) * SCREEN_W / dst.width, (m.y - dst.y) * SCREEN_H / dst.height};
}

bool Game::mPressed(int b) const {
    if (!sim.active) return IsMouseButtonPressed(b);
    return b == MOUSE_RIGHT_BUTTON ? sim.pressedR : sim.pressedL;
}
bool Game::mDown(int b) const {
    if (!sim.active) return IsMouseButtonDown(b);
    return b == MOUSE_RIGHT_BUTTON ? sim.downR : sim.downL;
}
bool Game::mReleased(int b) const {
    if (!sim.active) return IsMouseButtonReleased(b);
    return b == MOUSE_RIGHT_BUTTON ? sim.releasedR : sim.releasedL;
}
bool Game::kPressed(int k) const {
    return sim.active ? sim.keysPressed.count(k) > 0 : IsKeyPressed(k);
}
bool Game::kDown(int k) const {
    return sim.active ? sim.keysDown.count(k) > 0 : IsKeyDown(k);
}
