// Skin: vinyl — a turntable: the record spins while playing (the cover art
// rotates as the label), the tonearm tracks progress from the lead-in
// groove to the run-out, and the right panel carries the tags, time and a
// seekable progress bar. List pages are a plain warm-dark list.
// Click the platter to play/pause, the arm side buttons for prev/next.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 400, H = 300;

constexpr Col BG     {30, 27, 25};
constexpr Col WOOD   {74, 52, 38};        // plinth
constexpr Col WOOD_D {56, 38, 28};
constexpr Col PLATTER{50, 52, 56};
constexpr Col VINYL  {16, 16, 18};
constexpr Col GROOVE {30, 30, 34};
constexpr Col LABEL_C{188, 62, 48};       // paper label when no art
constexpr Col TEXT   {236, 232, 226};
constexpr Col DIM    {150, 142, 132};
constexpr Col ACCENT {214, 160, 62};      // brass

constexpr float REC_CX = 128, REC_CY = 158, REC_R = 104, LABEL_R = 36;
constexpr float PIV_X = 224, PIV_Y = 48;  // tonearm pivot, on the plinth
constexpr float ROW_H = 18, LIST_Y = 24;
constexpr SDL_FRect RC_BAR{252, 232, 136, 8};
constexpr SDL_FRect RC_PREV{252, 252, 40, 24};
constexpr SDL_FRect RC_NEXT{300, 252, 40, 24};

void thickLine(SDL_Renderer* r, float x0, float y0, float x1, float y1,
               float t, Col c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::hypot(dx, dy);
    if (len < 1) return;
    float nx = -dy / len * t / 2, ny = dx / len * t / 2;
    fillTriangle(r, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    fillTriangle(r, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

void drawTurntable(SDL_Renderer* r) {
    const Track* t = currentTrack();
    double dur = playback::duration(), pos = playback::position();
    float frac = dur > 0 ? (float)(pos / dur) : 0;
    bool spinning = playback::active() && !playback::paused();
    float ang = spinning ? (float)(SDL_GetTicks() % 3600000) * 0.00028f : 0.6f;

    // plinth
    fillRoundRect(r, {8, 20, 240, 268}, 10, WOOD_D);
    fillRoundRect(r, {12, 24, 232, 260}, 8, WOOD);

    // platter + record
    fillCircle(r, REC_CX, REC_CY + 3, REC_R + 10, {20, 20, 22});
    fillCircle(r, REC_CX, REC_CY, REC_R + 8, PLATTER);
    fillCircle(r, REC_CX, REC_CY, REC_R, VINYL);
    for (float gr = REC_R - 8; gr > LABEL_R + 8; gr -= 9)
        fillRing(r, REC_CX, REC_CY, gr, gr - 1, GROOVE);
    // sheen: a faint wedge that stays put while the record spins
    fillTriangle(r, REC_CX, REC_CY, REC_CX + REC_R * 0.94f, REC_CY - 30,
                 REC_CX + REC_R * 0.80f, REC_CY - 62, {26, 27, 31});

    // label: rotating cover art (or paper label), spindle on top
    if (g.art) {
        float side = LABEL_R * 1.4f;
        SDL_FRect dst{REC_CX - side / 2, REC_CY - side / 2, side, side};
        fillCircle(r, REC_CX, REC_CY, LABEL_R, {228, 222, 210});
        SDL_RenderTextureRotated(r, g.art, nullptr, &dst,
                                 ang * 180.0 / M_PI, nullptr, SDL_FLIP_NONE);
    } else {
        fillCircle(r, REC_CX, REC_CY, LABEL_R, LABEL_C);
        float mx = REC_CX + std::cos(ang) * (LABEL_R - 8);
        float my = REC_CY + std::sin(ang) * (LABEL_R - 8);
        fillCircle(r, mx, my, 3, {240, 226, 200});
        if (t)
            drawTextCentered(r, "33", REC_CX, REC_CY - 12, 1,
                             {240, 226, 200});
    }
    fillCircle(r, REC_CX, REC_CY, 4, {210, 212, 218});
    fillCircle(r, REC_CX, REC_CY, 2, {90, 92, 98});

    // tonearm: needle rides from the edge to the label as the track plays
    float rn = (REC_R - 6) - frac * ((REC_R - 6) - (LABEL_R + 6));
    float dirx = PIV_X - REC_CX, diry = PIV_Y - REC_CY;
    float dl = std::hypot(dirx, diry);
    float nX = REC_CX + dirx / dl * rn, nY = REC_CY + diry / dl * rn;
    if (!playback::active()) { nX = PIV_X - 10; nY = PIV_Y + 66; }  // rested
    fillCircle(r, PIV_X, PIV_Y, 13, {30, 30, 34});
    fillCircle(r, PIV_X, PIV_Y, 10, {168, 170, 178});
    // counterweight
    float cwx = PIV_X + (PIV_X - nX) * 0.16f, cwy = PIV_Y + (PIV_Y - nY) * 0.16f;
    thickLine(r, PIV_X, PIV_Y, cwx, cwy, 6, {120, 122, 130});
    fillCircle(r, cwx, cwy, 6, {80, 82, 90});
    thickLine(r, PIV_X, PIV_Y, nX, nY, 4, {200, 202, 210});
    fillCircle(r, nX, nY, 4, {60, 62, 70});   // headshell
    fillCircle(r, nX, nY, 1.5f, ACCENT);

    // right info panel
    float ix = 252, iw = W - ix - 10;
    if (t) {
        drawText(r, t->title, ix, 96, 1, TEXT, iw, true);
        std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                    : t->meta.artist;
        std::string album = t->meta.album.empty() ? Library::kUnknownAlbum
                                                  : t->meta.album;
        drawText(r, artist, ix, 114, 1, DIM, iw);
        drawText(r, album, ix, 130, 1, DIM, iw);
        if (t->meta.year)
            drawText(r, std::to_string(t->meta.year), ix, 146, 1, DIM);
        char q[32];
        std::snprintf(q, sizeof q, "side A - %d of %d", g.qpos + 1,
                      (int)g.queue.size());
        drawText(r, q, ix, 170, 1, ACCENT);
        std::string tm = fmtTime(pos) + " / " + fmtTime(dur);
        drawText(r, tm, ix, 212, 1, DIM);
    } else {
        drawText(r, "nothing on the", ix, 110, 1, DIM);
        drawText(r, "platter", ix, 126, 1, DIM);
    }
    drawText(r, "33 1/3 rpm", ix, 30, 1, ACCENT, 0, true);
    drawText(r, spinning ? "spinning" : "stopped", ix, 46, 1, DIM);

    // progress / volume bar
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    setCol(r, {60, 54, 48});
    fillR(r, RC_BAR.x, RC_BAR.y, RC_BAR.w, RC_BAR.h);
    setCol(r, ACCENT);
    if (volMode) {
        fillR(r, RC_BAR.x, RC_BAR.y, RC_BAR.w * playback::volume(), RC_BAR.h);
        drawText(r, "volume", RC_BAR.x, RC_BAR.y - 14, 1, DIM);
    } else {
        fillR(r, RC_BAR.x, RC_BAR.y, RC_BAR.w * frac, RC_BAR.h);
    }

    // prev / next
    for (int i = 0; i < 2; ++i) {
        SDL_FRect b = i ? RC_NEXT : RC_PREV;
        fillRoundRect(r, b, 4, {48, 42, 38});
        float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        if (i == 0) {
            fillTriangle(r, cx + 6, cy - 5, cx + 6, cy + 5, cx - 2, cy, TEXT);
            setCol(r, TEXT);
            fillR(r, cx - 6, cy - 5, 2, 10);
        } else {
            fillTriangle(r, cx - 6, cy - 5, cx - 6, cy + 5, cx + 2, cy, TEXT);
            setCol(r, TEXT);
            fillR(r, cx + 4, cy - 5, 2, 10);
        }
    }
}

void drawList(SDL_Renderer* r, Page& p) {
    setCol(r, {44, 38, 34});
    fillR(r, 0, 0, W, 20);
    if (g.nav.size() > 1) fillTriangle(r, 14, 4, 14, 16, 6, 10, DIM);
    drawTextCentered(r, p.title, W / 2, 6, 1, TEXT, true);

    const int visible = (int)((H - LIST_Y) / ROW_H);
    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;

    for (int i = 0; i < visible; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        float y = LIST_Y + i * ROW_H;
        bool sel = idx == p.cursor;
        if (sel) {
            setCol(r, {66, 52, 40});
            fillR(r, 0, y, W, ROW_H);
            setCol(r, ACCENT);
            fillR(r, 0, y, 3, ROW_H);
        }
        drawText(r, p.rows[idx].label, 12, y + 5, 1, sel ? TEXT : DIM, W - 32);
        if (p.rows[idx].arrow)
            drawTextRaw(r, ">", W - 14, y + 5, 1, sel ? TEXT : DIM);
    }
    if (p.rows.empty())
        drawTextCentered(r, "No songs found", W / 2, 120, 1, DIM);
}

void render(SDL_Renderer* r) {
    setCol(r, BG);
    fillR(r, 0, 0, W, H);
    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) drawTurntable(r);
    else drawList(r, p);
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;
    Page& p = g.nav.back();

    if (p.view != View::NOW_PLAYING) {
        if (y < 20) {
            if (x < 40 && g.nav.size() > 1) goBack();
            return;
        }
        int idx = p.scroll + (int)((y - LIST_Y) / ROW_H);
        if (idx >= 0 && idx < (int)p.rows.size()) {
            p.cursor = idx;
            activateRow(p, idx);
        }
        return;
    }

    auto in = [&](SDL_FRect rc) {
        return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
    };
    if (in({RC_BAR.x - 4, RC_BAR.y - 8, RC_BAR.w + 8, RC_BAR.h + 16})) {
        float f = std::clamp((x - RC_BAR.x) / RC_BAR.w, 0.0f, 1.0f);
        playback::seekTo(f * playback::duration());
        return;
    }
    if (in(RC_PREV)) { prevTrack(); return; }
    if (in(RC_NEXT)) { nextTrack(); return; }
    if (std::hypot(x - REC_CX, y - REC_CY) < REC_R + 8) playPauseToggle();
}

}  // namespace

const Skin kSkinVinyl = {"vinyl", W, H, 2, render, event};
