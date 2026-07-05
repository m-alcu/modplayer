// Skin: Apple iPod nano — portrait body, screen on top, working click wheel.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr float WIN_W = 340, WIN_H = 560;
constexpr SDL_FRect SCR{30, 30, 280, 210};            // the nano's screen
constexpr float HDR_H = 20;                           // screen title bar
constexpr float WHEEL_CX = 170, WHEEL_CY = 425;
constexpr float WHEEL_R = 112, HUB_R = 42;

void drawBattery(SDL_Renderer* r, float x, float y) {
    setCol(r, C_GRAY);
    fillR(r, x, y, 18, 9);
    fillR(r, x + 18, y + 2.5f, 2, 4);
    setCol(r, C_WHITE);
    fillR(r, x + 1, y + 1, 16, 7);
    SDL_SetRenderDrawColor(r, 92, 190, 90, 255);
    fillR(r, x + 1, y + 1, 16, 7);
}

void drawHeader(SDL_Renderer* r, const std::string& title) {
    vGradient(r, {SCR.x, SCR.y, SCR.w, HDR_H}, {245, 246, 248}, {198, 202, 210});
    setCol(r, {140, 145, 154});
    fillR(r, SCR.x, SCR.y + HDR_H - 1, SCR.w, 1);
    drawTextCentered(r, title, SCR.x + SCR.w / 2, SCR.y + 6, 1, C_TEXT, true);
    drawBattery(r, SCR.x + SCR.w - 26, SCR.y + 5);
    // play-state icon, top left
    if (playback::active()) {
        float ix = SCR.x + 8, iy = SCR.y + 5;
        if (playback::paused()) {
            setCol(r, C_GRAY);
            fillR(r, ix, iy, 3, 9);
            fillR(r, ix + 5, iy, 3, 9);
        } else {
            fillTriangle(r, ix, iy, ix, iy + 9, ix + 8, iy + 4.5f, C_GRAY);
        }
    }
}

void drawList(SDL_Renderer* r, Page& p) {
    const float rowH = 21;
    const float listY = SCR.y + HDR_H;
    const int visible = (int)((SCR.h - HDR_H) / rowH);  // 9 rows

    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;

    for (int i = 0; i < visible; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        float y = listY + i * rowH;
        bool sel = idx == p.cursor;
        if (sel)
            vGradient(r, {SCR.x, y, SCR.w, rowH}, C_SEL_TOP, C_SEL_BOT);
        Col tc = sel ? C_WHITE : C_TEXT;
        drawText(r, p.rows[idx].label, SCR.x + 8, y + 6, 1, tc, SCR.w - 26,
                 sel);
        if (p.rows[idx].arrow)
            drawTextRaw(r, ">", SCR.x + SCR.w - 12, y + 6, 1, tc, sel);
    }

    if (p.rows.empty()) {
        drawTextCentered(r, "No songs found", SCR.x + SCR.w / 2, SCR.y + 90, 1,
                         C_GRAY);
        drawTextCentered(r, "modplayer <mods folder>", SCR.x + SCR.w / 2,
                         SCR.y + 108, 1, C_GRAY);
    }

    // scrollbar
    if ((int)p.rows.size() > visible) {
        float trackH = SCR.h - HDR_H - 4;
        float barH = std::max(12.0f, trackH * visible / p.rows.size());
        float t = p.rows.size() - visible > 0
                      ? (float)p.scroll / (p.rows.size() - visible) : 0;
        setCol(r, {225, 227, 231});
        fillR(r, SCR.x + SCR.w - 5, listY + 2, 3, trackH);
        setCol(r, {150, 155, 164});
        fillR(r, SCR.x + SCR.w - 5, listY + 2 + t * (trackH - barH), 3, barH);
    }
}

void drawNowPlaying(SDL_Renderer* r) {
    const Track* t = currentTrack();
    drawHeader(r, "Now Playing");
    if (!t) {
        drawTextCentered(r, "Nothing playing", SCR.x + SCR.w / 2, SCR.y + 100,
                         1, C_GRAY);
        return;
    }

    char idxbuf[32];
    std::snprintf(idxbuf, sizeof idxbuf, "%d of %d", g.qpos + 1,
                  (int)g.queue.size());
    drawText(r, idxbuf, SCR.x + 10, SCR.y + HDR_H + 8, 1, C_GRAY);

    // cover art
    SDL_FRect box{SCR.x + 14, SCR.y + HDR_H + 24, 96, 96};
    if (g.art) {
        float tw, th;
        SDL_GetTextureSize(g.art, &tw, &th);
        float sc = std::min(box.w / tw, box.h / th);
        SDL_FRect dst{box.x + (box.w - tw * sc) / 2,
                      box.y + (box.h - th * sc) / 2, tw * sc, th * sc};
        setCol(r, {160, 164, 172});
        fillR(r, dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2);
        SDL_RenderTexture(r, g.art, nullptr, &dst);
    } else {
        setCol(r, {160, 164, 172});
        fillR(r, box.x - 1, box.y - 1, box.w + 2, box.h + 2);
        drawNoArt(r, box, {235, 237, 241}, {202, 206, 214}, {150, 155, 166});
    }

    // title / artist / album
    float tx = box.x + box.w + 12;
    float tmax = SCR.x + SCR.w - 10 - tx;
    drawText(r, t->title, tx, box.y + 8, 1, C_TEXT, tmax, true);
    std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                : t->meta.artist;
    std::string album = t->meta.album.empty() ? Library::kUnknownAlbum
                                              : t->meta.album;
    drawText(r, artist, tx, box.y + 28, 1, C_GRAY, tmax);
    drawText(r, album, tx, box.y + 46, 1, C_GRAY, tmax);
    if (t->meta.year)
        drawText(r, std::to_string(t->meta.year), tx, box.y + 64, 1, C_GRAY,
                 tmax);

    // progress / volume bar
    float bx = SCR.x + 14, bw = SCR.w - 28, by = SCR.y + SCR.h - 36;
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    setCol(r, {210, 213, 219});
    fillR(r, bx, by, bw, 8);
    if (volMode) {
        vGradient(r, {bx, by, bw * playback::volume(), 8}, C_SEL_TOP, C_SEL_BOT);
        drawTextCentered(r, "Volume", SCR.x + SCR.w / 2, by + 14, 1, C_GRAY);
    } else {
        double dur = playback::duration(), pos = playback::position();
        float frac = dur > 0 ? (float)(pos / dur) : 0;
        vGradient(r, {bx, by, bw * frac, 8}, C_SEL_TOP, C_SEL_BOT);
        // diamond knob, iPod style
        float kx = bx + bw * frac;
        fillTriangle(r, kx - 5, by + 4, kx, by - 2, kx + 5, by + 4, C_TEXT);
        fillTriangle(r, kx - 5, by + 4, kx, by + 10, kx + 5, by + 4, C_TEXT);
        drawText(r, fmtTime(pos), bx, by + 14, 1, C_GRAY);
        std::string rem = "-" + fmtTime(dur - pos);
        drawText(r, rem, bx + bw - textW(rem, 1), by + 14, 1, C_GRAY);
    }
    setCol(r, {160, 164, 172});  // bar outline
    SDL_FRect orc{bx, by, bw, 8};
    SDL_RenderRect(r, &orc);
}

void drawWheel(SDL_Renderer* r) {
    fillCircle(r, WHEEL_CX, WHEEL_CY + 2, WHEEL_R + 3, {170, 173, 180});
    fillCircle(r, WHEEL_CX, WHEEL_CY, WHEEL_R + 2, {205, 208, 214});
    fillCircle(r, WHEEL_CX, WHEEL_CY, WHEEL_R, {242, 243, 246});
    fillRing(r, WHEEL_CX, WHEEL_CY, HUB_R + 2, HUB_R, {205, 208, 214});
    fillCircle(r, WHEEL_CX, WHEEL_CY, HUB_R, {252, 252, 253});

    Col lc{150, 155, 164};
    drawTextCentered(r, "MENU", WHEEL_CX, WHEEL_CY - WHEEL_R + 18, 1, lc, true);

    // play/pause (bottom)
    float py = WHEEL_CY + WHEEL_R - 24;
    fillTriangle(r, WHEEL_CX - 10, py, WHEEL_CX - 10, py + 10, WHEEL_CX - 2,
                 py + 5, lc);
    setCol(r, lc);
    fillR(r, WHEEL_CX + 2, py, 3, 10);
    fillR(r, WHEEL_CX + 7, py, 3, 10);

    // prev |<< (left)
    float lx = WHEEL_CX - WHEEL_R + 16, ly = WHEEL_CY;
    setCol(r, lc);
    fillR(r, lx, ly - 5, 3, 10);
    fillTriangle(r, lx + 12, ly - 5, lx + 12, ly + 5, lx + 4, ly, lc);
    fillTriangle(r, lx + 20, ly - 5, lx + 20, ly + 5, lx + 12, ly, lc);

    // next >>| (right)
    float rx = WHEEL_CX + WHEEL_R - 16, ry = WHEEL_CY;
    setCol(r, lc);
    fillR(r, rx - 3, ry - 5, 3, 10);
    fillTriangle(r, rx - 24, ry - 5, rx - 24, ry + 5, rx - 16, ry, lc);
    fillTriangle(r, rx - 16, ry - 5, rx - 16, ry + 5, rx - 8, ry, lc);
}

void render(SDL_Renderer* ren) {
    SDL_SetRenderDrawColor(ren, 60, 62, 68, 255);
    SDL_RenderClear(ren);

    // device body
    fillRoundRect(ren, {8, 6, WIN_W - 16, WIN_H - 12}, 26, {126, 130, 138});
    fillRoundRect(ren, {10, 8, WIN_W - 20, WIN_H - 16}, 24, {236, 237, 240});
    vGradient(ren, {12, 30, 6, WIN_H - 60}, {252, 252, 253}, {214, 216, 221});

    // screen bezel + screen
    setCol(ren, {40, 42, 48});
    fillR(ren, SCR.x - 5, SCR.y - 5, SCR.w + 10, SCR.h + 10);
    setCol(ren, C_WHITE);
    fillR(ren, SCR.x, SCR.y, SCR.w, SCR.h);

    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) drawNowPlaying(ren);
    else {
        drawHeader(ren, g.nav.size() == 1 ? "iPod" : p.title);
        drawList(ren, p);
    }

    drawWheel(ren);
}

// Click-wheel zones on release-without-rotation.
void wheelClick(float dx, float dy, float dist) {
    if (dist <= HUB_R) { selectPress(); return; }
    float ang = std::atan2(dy, dx);  // 0 = right, pi/2 = down
    const float q = (float)M_PI / 4;
    if (ang > -3 * q && ang <= -q) goBack();                       // MENU (top)
    else if (ang > -q && ang <= q) nextTrack();                    // >>|
    else if (ang > q && ang <= 3 * q) playPauseToggle();           // play/pause
    else prevTrack();                                              // |<<
}

void event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            float dx = e.button.x - WHEEL_CX, dy = e.button.y - WHEEL_CY;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d <= WHEEL_R + 6) {
                g.wheelDown = true;
                g.wheelMoved = false;
                g.wheelAngle = std::atan2(dy, dx);
                g.wheelAccum = 0;
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (!g.wheelDown) break;
            float dx = e.motion.x - WHEEL_CX, dy = e.motion.y - WHEEL_CY;
            if (std::sqrt(dx * dx + dy * dy) < 12) break;  // too central: noisy
            float ang = std::atan2(dy, dx);
            float d = ang - g.wheelAngle;
            while (d > (float)M_PI) d -= 2 * (float)M_PI;
            while (d < -(float)M_PI) d += 2 * (float)M_PI;
            g.wheelAngle = ang;
            g.wheelAccum += d;
            const float step = 0.30f;  // radians per detent
            while (g.wheelAccum > step)  { g.wheelAccum -= step; scrollStep(+1); g.wheelMoved = true; }
            while (g.wheelAccum < -step) { g.wheelAccum += step; scrollStep(-1); g.wheelMoved = true; }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (!g.wheelDown) break;
            g.wheelDown = false;
            if (g.wheelMoved) break;
            float dx = e.button.x - WHEEL_CX, dy = e.button.y - WHEEL_CY;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d <= WHEEL_R + 6) wheelClick(dx, dy, d);
            break;
        }
        default: break;
    }
}

}  // namespace

const Skin kSkinNano = {"nano", (int)WIN_W, (int)WIN_H, 1, render, event};
