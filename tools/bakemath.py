#!/usr/bin/env python3
"""复现 bakeTerrain 坐标数学，定位整行透明的原因"""
import math

TILE_W, TILE_H = 64, 32
TERRAIN_SC = 0.5
W = H = 64  # 地图 64x64

terrainOX = (H - 1) * (TILE_W / 2)
terrainW = (W + H - 2) * (TILE_W / 2) + TILE_W
terrainH = (W + H - 2) * (TILE_H / 2) + TILE_H
bw = int(terrainW * TERRAIN_SC) + 1
bh = int(terrainH * TERRAIN_SC) + 1
print(f"terrainOX={terrainOX} terrainW={terrainW} terrainH={terrainH} bw={bw} bh={bh}")

def tHash(x, y, seed):
    h = ((x & 0xFFFFFFFF) * 73856093 & 0xFFFFFFFF) ^ ((y & 0xFFFFFFFF) * 19349663 & 0xFFFFFFFF) ^ ((seed & 0xFFFFFFFF) * 83492791 & 0xFFFFFFFF)
    h ^= h >> 13; h = (h * 0x5bd1e995) & 0xFFFFFFFF; h ^= h >> 15
    return h

def tNoise(x, y, stride, seed):
    gx = math.floor(x / stride); gy = math.floor(y / stride)
    fx = x / stride - gx; fy = y / stride - gy
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
    def h(ix, iy): return (tHash(ix, iy, seed) % 1024) / 1024.0
    a = h(gx, gy); b = h(gx + 1, gy); c = h(gx, gy + 1); d = h(gx + 1, gy + 1)
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy

seed = 20260723

# 对透明带行 by=150 与不透明带行 by=500，扫描 bx，统计 inBounds 命中率
for by in [50, 150, 280, 380, 500, 610, 720, 830, 940]:
    sy = by / TERRAIN_SC
    hits = 0
    samples = []
    for bx in range(0, bw, 64):
        sx = bx / TERRAIN_SC - terrainOX
        dith = tNoise(sx, sy, 9, seed + 7) - 0.5
        jx = sx + dith * 13.0; jy = sy + dith * 7.0
        fx = jx / (TILE_W / 2.0); fy = jy / (TILE_H / 2.0)
        tx = math.floor((fx + fy) / 2.0); ty = math.floor((fy - fx) / 2.0)
        ok = 0 <= tx < W and 0 <= ty < H
        hits += ok
        samples.append(f"bx={bx}:tx={tx},ty={ty}{'OK' if ok else 'X'}")
    print(f"by={by} sy={sy}: hits={hits}/{len(samples)}")
    if by in (150, 500):
        for s in samples[:8]: print("   ", s)
