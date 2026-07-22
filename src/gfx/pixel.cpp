#include "gfx/pixel.h"
#include "core/util.h"
#include <cmath>
#include <cstring>

void PixBuf::hline(int x0, int x1, int y, Color c) {
    if (y < 0 || y >= h) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0); x1 = std::min(w - 1, x1);
    for (int x = x0; x <= x1; x++) set(x, y, c);
}

void PixBuf::fillRect(int x, int y, int rw, int rh, Color c) {
    for (int j = y; j < y + rh; j++) hline(x, x + rw - 1, j, c);
}

void PixBuf::rect(int x, int y, int rw, int rh, Color c) {
    hline(x, x + rw - 1, y, c);
    hline(x, x + rw - 1, y + rh - 1, c);
    for (int j = y; j < y + rh; j++) { set(x, j, c); set(x + rw - 1, j, c); }
}

void PixBuf::line(int x0, int y0, int x1, int y1, Color c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        set(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void PixBuf::fillEllipse(int cx, int cy, int rx, int ry, Color c) {
    if (rx <= 0 || ry <= 0) return;
    for (int y = -ry; y <= ry; y++) {
        float t = 1.0f - (float)(y * y) / (float)(ry * ry);
        if (t < 0) continue;
        int xr = (int)(rx * sqrtf(t));
        hline(cx - xr, cx + xr, cy + y, c);
    }
}

void PixBuf::ellipse(int cx, int cy, int rx, int ry, Color c) {
    int steps = (rx + ry) * 4;
    for (int i = 0; i < steps; i++) {
        float a = (float)i / steps * 6.2831853f;
        set(cx + (int)(cosf(a) * rx), cy + (int)(sinf(a) * ry), c);
    }
}

void PixBuf::diamond(int cx, int cy, int rw, int rh, Color c) {
    for (int y = -rh; y <= rh; y++) {
        float t = 1.0f - fabsf((float)y) / (float)(rh + 1);
        int xr = (int)(rw * t);
        hline(cx - xr, cx + xr, cy + y, c);
    }
}

void PixBuf::isoBox(int cx, int topY, int rw, int rh, int height, Color top, Color left, Color right) {
    // 顶面菱形
    diamond(cx, topY, rw, rh, top);
    // 左右侧面（垂直向下的平行四边形）
    for (int y = 0; y < height; y++) {
        for (int k = 0; k <= rh; k++) {
            float t = 1.0f - (float)k / (float)(rh + 1);
            int xr = (int)(rw * t);
            // 左半
            hline(cx - xr, cx, topY + k + y, left);
            // 右半
            hline(cx, cx + xr, topY + k + y, right);
        }
    }
}

void PixBuf::blit(const PixBuf& src, int dx, int dy) {
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            Color c = src.get(x, y);
            if (c.a > 0) blend(dx + x, dy + y, c);
        }
}

PixBuf PixBuf::flipH() const {
    PixBuf r(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            r.set(w - 1 - x, y, get(x, y));
    return r;
}

PixBuf PixBuf::scale(int nw, int nh) const {
    PixBuf r(nw, nh);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++)
            r.set(x, y, get(x * w / nw, y * h / nh));
    return r;
}

PixBuf PixBuf::rotate8(int dir45) const {
    dir45 &= 7;
    if (dir45 == 0) return *this;
    if (dir45 == 4) { // 180
        PixBuf r(w, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                r.set(w - 1 - x, h - 1 - y, get(x, y));
        return r;
    }
    float ang = dir45 * 3.14159265f / 4.0f;
    float cs = cosf(ang), sn = sinf(ang);
    // 输出尺寸 = 旋转后包围盒
    int nw = (int)(fabsf(w * cs) + fabsf(h * sn)) + 1;
    int nh = (int)(fabsf(w * sn) + fabsf(h * cs)) + 1;
    PixBuf r(nw, nh);
    float cx0 = w / 2.0f, cy0 = h / 2.0f;
    float cx1 = nw / 2.0f, cy1 = nh / 2.0f;
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            float dx = x - cx1, dy = y - cy1;
            int sx = (int)(dx * cs + dy * sn + cx0);
            int sy = (int)(-dx * sn + dy * cs + cy0);
            Color c = get(sx, sy);
            if (c.a > 0) r.set(x, y, c);
        }
    return r;
}

void PixBuf::remap(Color from, Color to) {
    // 按 hue 接近 REMAP 系的像素做替换：保持亮度比例
    for (auto& c : px) {
        if (c.a == 0) continue;
        if (c.r > 150 && c.g < 90 && c.b < 90) { // 红色系占位
            float lum = c.r / 255.0f;
            c.r = (uint8_t)std::min(255, (int)(to.r * lum + (to.r > 40 ? 0 : 0)));
            c.g = (uint8_t)std::min(255, (int)(to.g * lum));
            c.b = (uint8_t)std::min(255, (int)(to.b * lum));
        }
    }
}

void PixBuf::shade(float f) {
    for (auto& c : px) {
        if (c.a == 0) continue;
        c.r = (uint8_t)clampi((int)(c.r * f), 0, 255);
        c.g = (uint8_t)clampi((int)(c.g * f), 0, 255);
        c.b = (uint8_t)clampi((int)(c.b * f), 0, 255);
    }
}

Texture2D PixBuf::toTexture() const {
    Image img;
    img.data = (void*)px.data();
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    return t;
}
