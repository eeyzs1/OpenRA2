#include "game/game.h"

Vector2 Game::mousePos() const {
    if (sim.active) return sim.pos;
    // 物理像素 → 逻辑画布坐标：与 render() 的 letterbox 目标矩形一一对应。
    // 高 DPI 显示器（如 150% 缩放）下 raylib 返回物理像素坐标，必须归一化，
    // 否则所有按钮命中测试整体偏移，表现为"点任何按钮都没反应"。
    float rw = (float)GetRenderWidth(), rh = (float)GetRenderHeight();
    Vector2 m = GetMousePosition();
    if (rw <= 0 || rh <= 0) return m;
    Rectangle dst{(rw - rh * SCREEN_W / SCREEN_H) / 2, 0, rh * SCREEN_W / SCREEN_H, rh};
    if (rw / SCREEN_W < rh / SCREEN_H) dst = Rectangle{0, (rh - rw * SCREEN_H / SCREEN_W) / 2, rw, rw * SCREEN_H / SCREEN_W};
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

