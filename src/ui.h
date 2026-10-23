#pragma once
#include <stdint.h>

/* 160x80 RGB565 procedural vector UI for MiniKey Forge.
 * States:
 *   0: Connected Boot Intro (age < 900ms: ring sweep + Face ID padlock lock-in)
 *      -> Connected Ambient Idle (age >= 900ms: breathing lock + orbiting comet)
 *   1: Touch Confirmation Prompt (dual orbiting comets + expanding sonar waves + live touch press)
 *   2: Touch Accepted / Cryptographic Verification (contracting energy ring + VERIFY badge)
 *   3: Apple Pay / Face ID Confirmed Success (emerald shockwave + elastic two-stroke checkmark + sparkles)
 *   4: Auth Denied / Cancelled (iOS horizontal damped shake + progressive coral X)
 *   5: Storage Fault (persistent coral X + FAULT badge)
 */

static inline unsigned ui_isqrt(unsigned n) {
    if (!n) return 0;
    unsigned x = n, y = (x + 1u) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

static inline uint16_t ui_blend565(uint16_t bg, uint16_t fg, int alpha) {
    if (alpha <= 0) return bg;
    if (alpha >= 256) return fg;
    int inv = 256 - alpha;
    unsigned r = ((((bg >> 11) & 31u) * (unsigned)inv) + (((fg >> 11) & 31u) * (unsigned)alpha)) >> 8;
    unsigned g = ((((bg >> 5) & 63u) * (unsigned)inv) + (((fg >> 5) & 63u) * (unsigned)alpha)) >> 8;
    unsigned b = (((bg & 31u) * (unsigned)inv) + ((fg & 31u) * (unsigned)alpha)) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline int ui_abs(int v) { return v < 0 ? -v : v; }

static inline unsigned ui_angle256(int dx, int dy) {
    int ax = ui_abs(dx), ay = ui_abs(dy);
    if (!ax && !ay) return 0;
    int hi = ax > ay ? ax : ay;
    int lo = ax > ay ? ay : ax;
    int r = (lo * 32) / hi;
    if (r > 31) r = 31;
    if (dx >= 0 && dy < 0) return (unsigned)(ax <= ay ? r : 64 - r);
    if (dx > 0 && dy >= 0) return (unsigned)(ax >= ay ? 64 + r : 128 - r);
    if (dx <= 0 && dy > 0) return (unsigned)(ax <= ay ? 128 + r : 192 - r);
    return (unsigned)(ax >= ay ? 192 + r : (256 - r) & 255);
}

static inline int ui_seg_dist_q2(int x, int y, int ax, int ay, int bx, int by) {
    int dx = bx - ax, dy = by - ay, den = dx * dx + dy * dy;
    if (!den) return ((x - ax) * (x - ax) + (y - ay) * (y - ay)) * 16;
    int t = ((x - ax) * dx + (y - ay) * dy) * 256 / den;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    int qx = (ax * 256 + dx * t) - x * 256;
    int qy = (ay * 256 + dy * t) - y * 256;
    return (qx * qx + qy * qy) / 4096;
}

static const struct { char ch; uint8_t row[7]; } ui_font[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
};

static inline int ui_glyph_bit(const char *text, int len, int x, int y) {
    if (y < 0 || y >= 7 || x < 0) return 0;
    int idx = x / 6, col = x % 6;
    if (col == 5 || idx >= len) return 0;
    char ch = text[idx];
    if (ch == ' ') return 0;
    for (unsigned i = 0; i < sizeof(ui_font) / sizeof(ui_font[0]); i++) {
        if (ui_font[i].ch == ch) return (ui_font[i].row[y] >> (4 - col)) & 1;
    }
    return 0;
}

static const int8_t ui_shake[14] = {0, 5, 3, -5, -3, 4, 2, -3, -1, 2, 1, -1, 0, 0};

static inline uint16_t ui_hero_pixel(int x, int y, unsigned state, uint32_t age, int touch_down) {
    const uint16_t bg = 0x0843;
    if ((state == 4 || state == 5) && age < 420) {
        x -= ui_shake[age / 30];
    }
    int dx = x - 32, dy = y - 32;
    int rq = (int)ui_isqrt((unsigned)(dx * dx + dy * dy) * 16u);
    uint16_t c = bg;

    if (state == 0) {
        if (age < 900) {
            int ring_rq = age < 300 ? (72 + (int)(age * 20u / 300u)) : 92;
            unsigned sweep = age < 450 ? (unsigned)(age * 256u / 450u) : 256u;
            unsigned ang = ui_angle256(dx, dy);
            if (rq < ring_rq - 4) {
                c = ui_blend565(bg, 0x0906, (ring_rq - rq) * 2);
            }
            if (age >= 450 && age < 860) {
                int wave_rq = 92 + (int)((age - 450u) * 32u / 410u);
                int wd = ui_abs(rq - wave_rq);
                if (wd < 8) {
                    int wa = (8 - wd) * (int)(860u - age) * 28 / 410;
                    c = ui_blend565(c, 0x5E5F, wa);
                }
            }
            int rd = ui_abs(rq - ring_rq);
            if (rd < 8 && ang <= sweep) {
                int ra = (8 - rd) * 32;
                uint16_t rc = (sweep - ang > 24u) ? 0x36BF : 0xDFFF;
                c = ui_blend565(c, rc, ra);
            }
            int lift = age < 180 ? 4 : (age < 450 ? (int)(4u * (450u - age) / 270u) : 0);
            int flash = (age >= 450 && age < 620) ? (int)(620u - age) : 0;
            uint16_t lock_col = ui_blend565(0x5E5F, 0xFFFF, flash);
            int sdx = x - 32, sdy = y - (27 - lift);
            if (sdy <= 0) {
                int srq = (int)ui_isqrt((unsigned)(sdx * sdx + sdy * sdy) * 16u);
                int sd = ui_abs(srq - 22);
                if (sd < 6) c = ui_blend565(c, lock_col, (6 - sd) * 42);
            } else if (y <= 31) {
                int left_leg = ui_abs(sdx * 2 + 11) <= 2;
                int right_leg = (ui_abs(sdx * 2 - 11) <= 2) && (lift == 0 || y <= 28 - (lift >> 1));
                if (left_leg || right_leg) c = lock_col;
            }
            if (x >= 24 && x <= 40 && y >= 31 && y <= 44) {
                if (!((x == 24 || x == 40) && (y == 31 || y == 44))) {
                    if (x >= 31 && x <= 33 && y >= 35 && y <= 40) {
                        c = 0x0843;
                    } else {
                        uint16_t body_c = ui_blend565(0x1B9F, 0x5E5F, (44 - y) * 16);
                        c = ui_blend565(body_c, 0xFFFF, flash);
                    }
                }
            }
            return c;
        } else {
            uint32_t idle_age = age - 900u;
            int phase = (int)((idle_age / 10u) & 255u);
            int breath = phase < 128 ? phase : 255 - phase;
            if (rq < 88) c = ui_blend565(bg, 0x0906, 60 + (breath >> 1));
            int rd = ui_abs(rq - 92);
            if (rd < 8) {
                unsigned ang = ui_angle256(dx, dy);
                unsigned comet = (idle_age / 8u) & 255u;
                unsigned diff = (comet - ang) & 255u;
                uint16_t ring_c = ui_blend565(0x1316, 0x36BF, breath + 64);
                if (diff < 48u) ring_c = ui_blend565(ring_c, 0xB7FF, (int)(48u - diff) * 5);
                c = ui_blend565(c, ring_c, (8 - rd) * 32);
            }
            int sdx = x - 32, sdy = y - 27;
            if (sdy <= 0) {
                int srq = (int)ui_isqrt((unsigned)(sdx * sdx + sdy * sdy) * 16u);
                int sd = ui_abs(srq - 22);
                if (sd < 6) c = ui_blend565(c, 0x7EFF, (6 - sd) * 42);
            } else if (y <= 31) {
                if (ui_abs(ui_abs(sdx * 2) - 11) <= 2) c = 0x7EFF;
            }
            if (x >= 24 && x <= 40 && y >= 31 && y <= 44 && !((x == 24 || x == 40) && (y == 31 || y == 44))) {
                if (x >= 31 && x <= 33 && y >= 35 && y <= 40) {
                    c = 0x0843;
                } else {
                    c = ui_blend565(0x1B9F, 0x5E5F, (44 - y) * 14 + (breath >> 1));
                }
            }
            return c;
        }
    }

    if (state == 1 || state == 2) {
        int active = (state == 2) || (touch_down != 0);
        uint16_t accent = active ? 0x368B : 0x0C3F;
        uint16_t bright = active ? 0x7FF3 : 0x5E5F;
        if (rq < 90) {
            c = ui_blend565(bg, active ? 0x09E3 : 0x0926, (90 - rq) * 2);
        }
        for (int k = 0; k < 2; k++) {
            int prog = (int)(((age / 10u) + (unsigned)(k * 40)) % 80u);
            int wrq = active ? (88 - prog) : (16 + prog);
            int wd = ui_abs(rq - wrq);
            if (wd < 6) {
                int wa = (6 - wd) * (80 - prog) / 2;
                c = ui_blend565(c, bright, wa);
            }
        }
        int rd = ui_abs(rq - 92);
        if (rd < 8) {
            unsigned ang = ui_angle256(dx, dy);
            unsigned rot = (age / (active ? 3u : 6u)) & 255u;
            unsigned d1 = (rot - ang) & 255u;
            unsigned d2 = ((rot + 128u) - ang) & 255u;
            unsigned da = d1 < d2 ? d1 : d2;
            if (state == 2) {
                int hi = da < 64u ? (int)(64u - da) * 4 : 0;
                uint16_t rc = ui_blend565(0x368B, 0xFFFF, hi);
                c = ui_blend565(c, rc, (8 - rd) * 32);
            } else if (da < 88u) {
                uint16_t rc = ui_blend565(accent, bright, (int)(88u - da) * 2);
                if (da < 14u) rc = ui_blend565(rc, 0xFFFF, (int)(14u - da) * 16);
                c = ui_blend565(c, rc, (8 - rd) * (int)(88u - da) / 2);
            } else {
                c = ui_blend565(c, 0x1126, (8 - rd) * 20);
            }
        }
        if (rq < 22) {
            c = ui_blend565(c, active ? 0xB7FF : 0x36BF, (22 - rq) * 11);
        }
        return c;
    }

    if (state == 3) {
        if (age < 420) {
            int wave_rq = 88 + (int)(age * 36u / 420u);
            int wd = ui_abs(rq - wave_rq);
            if (wd < 8) {
                int wa = (8 - wd) * (int)(420u - age) * 30 / 420;
                c = ui_blend565(c, 0x368B, wa);
            }
        }
        if (age >= 400 && age < 820) {
            int sp = 210 - ui_abs((int)age - 610);
            static const int8_t sp_xy[4][2] = {{10, 12}, {54, 12}, {10, 52}, {54, 52}};
            for (int i = 0; i < 4; i++) {
                int adx = ui_abs(x - sp_xy[i][0]), ady = ui_abs(y - sp_xy[i][1]);
                if ((adx == 0 && ady <= 3) || (ady == 0 && adx <= 3) || (adx + ady <= 2)) {
                    c = ui_blend565(c, 0x7FF3, sp);
                }
            }
        }
        int badge_rq = age < 120 ? (64 + (int)(age * 24u / 120u)) : 88;
        if (rq < badge_rq) {
            uint16_t fill_c = ui_blend565(0x09E3, 0x13A6, (64 - y) * 3);
            if (age >= 420 && age < 820) {
                int sheen_pos = -36 + (int)((age - 420u) * 72u / 400u);
                int sd = ui_abs((dx + dy) - sheen_pos);
                if (sd < 9) fill_c = ui_blend565(fill_c, 0x368B, (9 - sd) * 20);
            }
            c = fill_c;
        }
        int rd = ui_abs(rq - badge_rq);
        if (rd < 8) {
            c = ui_blend565(c, 0x368B, (8 - rd) * 32);
        }
        int ax = 20, ay = 33, bx = 28, by = 41;
        int over = (age >= 350 && age < 450) ? 2 : ((age >= 450 && age < 500) ? 1 : 0);
        int cx = 45 + over, cy = 23 - over;
        int dist = 9999;
        if (age > 80) {
            int t1 = age < 230 ? (int)((age - 80u) * 256u / 150u) : 256;
            dist = ui_seg_dist_q2(x, y, ax, ay, ax + (bx - ax) * t1 / 256, ay + (by - ay) * t1 / 256);
        }
        if (age > 230) {
            int t2 = age < 440 ? (int)((age - 230u) * 256u / 210u) : 256;
            int d2 = ui_seg_dist_q2(x, y, bx, by, bx + (cx - bx) * t2 / 256, by + (cy - by) * t2 / 256);
            if (d2 < dist) dist = d2;
        }
        if (dist < 96) {
            c = (dist <= 36) ? 0xFFFF : ui_blend565(c, 0x7FF3, (96 - dist) * 4);
        }
        return c;
    }

    if (rq < 88) c = 0x2841;
    int rd = ui_abs(rq - 88);
    if (rd < 8) c = ui_blend565(c, 0xFA27, (8 - rd) * 32);
    int t1 = age < 140 ? (int)(age * 256u / 140u) : 256;
    int d1 = ui_seg_dist_q2(x, y, 23, 23, 23 + 18 * t1 / 256, 23 + 18 * t1 / 256);
    int d2 = 9999;
    if (age > 120) {
        int t2 = age < 260 ? (int)((age - 120u) * 256u / 140u) : 256;
        d2 = ui_seg_dist_q2(x, y, 41, 23, 41 - 18 * t2 / 256, 23 + 18 * t2 / 256);
    }
    int d = d1 < d2 ? d1 : d2;
    if (d < 88) {
        c = (d <= 32) ? 0xFFFF : ui_blend565(c, 0xFA27, (88 - d) * 4);
    }
    return c;
}

static inline uint16_t ui_bar_pixel(int bx, unsigned state, uint32_t age, int touch_down) {
    const uint16_t track = 0x1107;
    if (state == 0 && age < 900) {
        int fill = (int)(age * 64u / 800u);
        return bx <= fill ? 0x5E5F : track;
    }
    if (state == 0) {
        int head = (int)(((age - 900u) / 20u) % 64u);
        int d = ui_abs(bx - head);
        return ui_blend565(0x1189, 0x5E5F, d < 16 ? (16 - d) * 16 : 0);
    }
    if (state == 1) {
        if (touch_down) return 0x7FF3;
        int head = (int)((age / 8u) % 64u);
        int d = (head - bx + 64) % 64;
        return ui_blend565(track, 0x5E5F, d < 24 ? (24 - d) * 10 : 0);
    }
    if (state == 2) return 0x368B;
    if (state == 3) {
        int sheen = (int)((age / 8u) % 80u) - 8;
        return ui_abs(bx - sheen) < 8 ? 0x7FF3 : 0x368B;
    }
    return 0xFA27;
}

static inline uint16_t ui_card_pixel(int cx, int cy, unsigned state, uint32_t age, int touch_down) {
    const uint16_t bg = 0x0843;
    const uint16_t card_bg = 0x08A4;
    /* Rounded rect SDF for 76x60 card (cx=0..75, cy=0..59), corner radius 6 */
    int qx = ui_abs(cx * 2 - 75) - (75 - 12);
    int qy = ui_abs(cy * 2 - 59) - (59 - 12);
    if (qx < 0) qx = 0;
    if (qy < 0) qy = 0;
    int d2 = qx * qx + qy * qy;
    if (d2 > 160) return bg;

    uint16_t border_c = 0x198A;
    if (state == 1) border_c = touch_down ? 0x368B : 0x0C3F;
    else if (state == 2 || state == 3) border_c = 0x2489;
    else if (state >= 4) border_c = 0x8924;

    if (d2 >= 100 || cx == 0 || cx == 75 || cy == 0 || cy == 59) {
        return border_c;
    }

    /* Top status dot (cx=9, cy=9) + "FIDO2 KEY" (cx=16, cy=6..12) */
    if ((cx - 9) * (cx - 9) + (cy - 9) * (cy - 9) <= 5) {
        if (state == 3 || (state == 1 && touch_down)) return 0x368B;
        if (state >= 4) return 0xFA27;
        return 0x5E5F;
    }
    if (ui_glyph_bit("FIDO2 KEY", 9, cx - 16, cy - 6)) {
        return 0x953F;
    }

    /* Center Pill Badge (cx=6..69, cy=18..38) */
    if (cx >= 6 && cx <= 69 && cy >= 18 && cy <= 38) {
        if (!((cx == 6 || cx == 69) && (cy == 18 || cy == 38))) {
            uint16_t pill_bg = 0x1149, pill_fg = 0xDFFF;
            const char *label = "READY";
            int len = 5;
            if (state == 0 && age < 900) {
                pill_bg = 0x0A54; pill_fg = 0xFFFF; label = "ONLINE"; len = 6;
            } else if (state == 1) {
                if (touch_down) {
                    pill_bg = 0x13A6; pill_fg = 0xFFFF; label = "HOLD"; len = 4;
                } else {
                    pill_bg = 0x0C3F; pill_fg = 0xFFFF; label = "TOUCH"; len = 5;
                }
            } else if (state == 2) {
                pill_bg = 0x13A6; pill_fg = 0xFFFF; label = "VERIFY"; len = 6;
            } else if (state == 3) {
                pill_bg = 0x1C88; pill_fg = 0xFFFF; label = "PASSED"; len = 6;
            } else if (state == 4) {
                pill_bg = 0x78E3; pill_fg = 0xFFFF; label = "DENIED"; len = 6;
            } else if (state >= 5) {
                pill_bg = 0x78E3; pill_fg = 0xFFFF; label = "FAULT"; len = 5;
            }
            int tx = cx - (38 - len * 3);
            int ty = cy - 25;
            if (ui_glyph_bit(label, len, tx, ty)) return pill_fg;
            return pill_bg;
        }
    }

    /* Bottom Progress / Wave Bar (cx=6..69, cy=46..51) */
    if (cx >= 6 && cx <= 69 && cy >= 46 && cy <= 51) {
        return ui_bar_pixel(cx - 6, state, age, touch_down);
    }

    return card_bg;
}

static inline uint16_t ui_screen_pixel(int sx, int sy, unsigned state, uint32_t age, int touch_down) {
    if (sx >= 8 && sx < 72 && sy >= 8 && sy < 72) {
        return ui_hero_pixel(sx - 8, sy - 8, state, age, touch_down);
    }
    if (sx >= 78 && sx < 154 && sy >= 10 && sy < 70) {
        return ui_card_pixel(sx - 78, sy - 10, state, age, touch_down);
    }
    return 0x0843;
}
