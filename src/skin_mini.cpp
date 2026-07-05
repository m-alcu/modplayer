// Skin: mini — a neutral, high-contrast 320x240 landscape UI meant for small
// displays (Raspberry Pi, car head units, touch screens). Big tap targets:
// list rows activate on a single tap and a bottom toolbar carries the
// transport; run with --fullscreen on the Pi console.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 320, H = 240;
constexpr float HDR = 22, TOOL = 32;
constexpr float LIST_Y = HDR, LIST_H = H - HDR - TOOL;
constexpr float ROW_H = 18;

constexpr Col BG    {24, 26, 31};
constexpr Col PANEL {36, 39, 47};
constexpr Col TEXT  {232, 234, 240};
constexpr Col DIM   {140, 146, 158};
constexpr Col ACCENT{74, 144, 217};
constexpr Col SEL_BG{38, 64, 105};

// toolbar buttons: vol-, prev, play/pause, next, vol+
constexpr int NBTN = 5;
SDL_FRect btnRect(int i) {
    float w = (float)W / NBTN;
    return {i * w, H - TOOL, w, TOOL};
}

void drawHeader(SDL_Renderer* r, const std::string& title, bool canBack) {
    setCol(r, PANEL);
    fillR(r, 0, 0, W, HDR);
    setCol(r, {50, 54, 64});
    fillR(r, 0, HDR - 1, W, 1);
    if (canBack) fillTriangle(r, 16, 5, 16, HDR - 5, 7, HDR / 2, DIM);
    drawTextCentered(r, title, W / 2, 7, 1, TEXT, true);
    if (playback::active()) {
        float ix = W - 20, iy = 6;
        if (playback::paused()) {
            setCol(r, DIM);
            fillR(r, ix, iy, 3, 10);
            fillR(r, ix + 5, iy, 3, 10);
        } else {
            fillTriangle(r, ix, iy, ix, iy + 10, ix + 9, iy + 5, ACCENT);
        }
    }
}

void drawList(SDL_Renderer* r, Page& p) {
    const int visible = (int)(LIST_H / ROW_H);  // 10 rows
    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;

    for (int i = 0; i < visible; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        float y = LIST_Y + i * ROW_H;
        bool sel = idx == p.cursor;
        if (sel) {
            setCol(r, SEL_BG);
            fillR(r, 0, y, W, ROW_H);
            setCol(r, ACCENT);
            fillR(r, 0, y, 3, ROW_H);
        }
        drawText(r, p.rows[idx].label, 12, y + 5, 1, sel ? TEXT : Col{200, 204, 213},
                 W - 34);
        if (p.rows[idx].arrow)
            drawTextRaw(r, ">", W - 16, y + 5, 1, sel ? TEXT : DIM);
    }

    if (p.rows.empty()) {
        drawTextCentered(r, "No songs found", W / 2, LIST_Y + 60, 1, DIM);
        drawTextCentered(r, "modplayer <mods folder>", W / 2, LIST_Y + 78, 1,
                         DIM);
    }

    if ((int)p.rows.size() > visible) {
        float trackH = LIST_H - 8;
        float barH = std::max(14.0f, trackH * visible / p.rows.size());
        float t = (float)p.scroll / (p.rows.size() - visible);
        setCol(r, {46, 50, 60});
        fillR(r, W - 5, LIST_Y + 4, 3, trackH);
        setCol(r, DIM);
        fillR(r, W - 5, LIST_Y + 4 + t * (trackH - barH), 3, barH);
    }
}

constexpr SDL_FRect RC_BAR{148, 178, 158, 8};  // progress bar (Now Playing)

void drawNowPlaying(SDL_Renderer* r) {
    const Track* t = currentTrack();
    if (!t) {
        drawTextCentered(r, "Nothing playing", W / 2, 100, 1, DIM);
        return;
    }

    // cover art, left
    SDL_FRect box{14, LIST_Y + 12, 120, 120};
    if (g.art) {
        float tw, th;
        SDL_GetTextureSize(g.art, &tw, &th);
        float sc = std::min(box.w / tw, box.h / th);
        SDL_FRect dst{box.x + (box.w - tw * sc) / 2,
                      box.y + (box.h - th * sc) / 2, tw * sc, th * sc};
        setCol(r, {70, 75, 88});
        fillR(r, dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2);
        SDL_RenderTexture(r, g.art, nullptr, &dst);
    } else {
        setCol(r, {70, 75, 88});
        fillR(r, box.x - 1, box.y - 1, box.w + 2, box.h + 2);
        drawNoArt(r, box, {44, 48, 58}, {30, 33, 40}, {96, 102, 116});
    }
    char idxbuf[32];
    std::snprintf(idxbuf, sizeof idxbuf, "%d of %d", g.qpos + 1,
                  (int)g.queue.size());
    drawText(r, idxbuf, box.x, box.y + box.h + 8, 1, DIM);

    // metadata, right
    float tx = box.x + box.w + 14, tmax = W - 12 - tx;
    drawText(r, t->title, tx, box.y + 4, 1, TEXT, tmax, true);
    std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                : t->meta.artist;
    std::string album = t->meta.album.empty() ? Library::kUnknownAlbum
                                              : t->meta.album;
    drawText(r, artist, tx, box.y + 26, 1, DIM, tmax);
    drawText(r, album, tx, box.y + 44, 1, DIM, tmax);
    if (t->meta.year)
        drawText(r, std::to_string(t->meta.year), tx, box.y + 62, 1, DIM, tmax);

    // progress / volume bar
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    setCol(r, {50, 54, 64});
    fillR(r, RC_BAR.x, RC_BAR.y, RC_BAR.w, RC_BAR.h);
    if (volMode) {
        setCol(r, ACCENT);
        fillR(r, RC_BAR.x, RC_BAR.y, RC_BAR.w * playback::volume(), RC_BAR.h);
        drawText(r, "Volume", RC_BAR.x, RC_BAR.y + 12, 1, DIM);
    } else {
        double dur = playback::duration(), pos = playback::position();
        float frac = dur > 0 ? (float)(pos / dur) : 0;
        setCol(r, ACCENT);
        fillR(r, RC_BAR.x, RC_BAR.y, RC_BAR.w * frac, RC_BAR.h);
        setCol(r, TEXT);
        fillR(r, RC_BAR.x + RC_BAR.w * frac - 1, RC_BAR.y - 2, 3, RC_BAR.h + 4);
        drawText(r, fmtTime(pos), RC_BAR.x, RC_BAR.y + 12, 1, DIM);
        std::string rem = "-" + fmtTime(dur - pos);
        drawText(r, rem, RC_BAR.x + RC_BAR.w - textW(rem, 1), RC_BAR.y + 12, 1,
                 DIM);
    }
}

void drawToolbar(SDL_Renderer* r) {
    setCol(r, PANEL);
    fillR(r, 0, H - TOOL, W, TOOL);
    setCol(r, {50, 54, 64});
    fillR(r, 0, H - TOOL, W, 1);

    Col ic{205, 209, 218};
    for (int i = 0; i < NBTN; ++i) {
        SDL_FRect b = btnRect(i);
        if (i) { setCol(r, {50, 54, 64}); fillR(r, b.x, b.y + 6, 1, TOOL - 12); }
        float cx = b.x + b.w / 2, cy = b.y + TOOL / 2;
        switch (i) {
            case 0:  // vol -
                setCol(r, ic);
                fillR(r, cx - 6, cy - 1.5f, 12, 3);
                break;
            case 1:  // |<<
                setCol(r, ic);
                fillR(r, cx - 10, cy - 6, 3, 12);
                fillTriangle(r, cx + 2, cy - 6, cx + 2, cy + 6, cx - 7, cy, ic);
                fillTriangle(r, cx + 11, cy - 6, cx + 11, cy + 6, cx + 2, cy, ic);
                break;
            case 2:  // play / pause
                if (playback::active() && !playback::paused()) {
                    setCol(r, ic);
                    fillR(r, cx - 6, cy - 7, 4, 14);
                    fillR(r, cx + 2, cy - 7, 4, 14);
                } else {
                    fillTriangle(r, cx - 5, cy - 8, cx - 5, cy + 8, cx + 9, cy,
                                 ic);
                }
                break;
            case 3:  // >>|
                fillTriangle(r, cx - 11, cy - 6, cx - 11, cy + 6, cx - 2, cy, ic);
                fillTriangle(r, cx - 2, cy - 6, cx - 2, cy + 6, cx + 7, cy, ic);
                setCol(r, ic);
                fillR(r, cx + 7, cy - 6, 3, 12);
                break;
            case 4:  // vol +
                setCol(r, ic);
                fillR(r, cx - 6, cy - 1.5f, 12, 3);
                fillR(r, cx - 1.5f, cy - 6, 3, 12);
                break;
        }
    }
}

void render(SDL_Renderer* r) {
    setCol(r, BG);
    fillR(r, 0, 0, W, H);

    Page& p = g.nav.back();
    drawHeader(r, p.title, g.nav.size() > 1);
    if (p.view == View::NOW_PLAYING) drawNowPlaying(r);
    else drawList(r, p);
    drawToolbar(r);
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;

    if (y < HDR) {                       // header: back arrow area
        if (x < 40 && g.nav.size() > 1) goBack();
        return;
    }
    if (y >= H - TOOL) {                 // toolbar
        int i = std::clamp((int)(x / ((float)W / NBTN)), 0, NBTN - 1);
        switch (i) {
            case 0: playback::setVolume(playback::volume() - 0.05f);
                    g.volOverlayUntil = SDL_GetTicks() + 1200; break;
            case 1: prevTrack(); break;
            case 2: playPauseToggle(); break;
            case 3: nextTrack(); break;
            case 4: playback::setVolume(playback::volume() + 0.05f);
                    g.volOverlayUntil = SDL_GetTicks() + 1200; break;
        }
        return;
    }

    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) {
        // tap the progress bar to seek
        if (x >= RC_BAR.x - 4 && x < RC_BAR.x + RC_BAR.w + 4 &&
            y >= RC_BAR.y - 8 && y < RC_BAR.y + RC_BAR.h + 8) {
            float f = std::clamp((x - RC_BAR.x) / RC_BAR.w, 0.0f, 1.0f);
            playback::seekTo(f * playback::duration());
        }
        return;
    }
    int idx = p.scroll + (int)((y - LIST_Y) / ROW_H);  // tap a row: activate it
    if (idx >= 0 && idx < (int)p.rows.size()) {
        p.cursor = idx;
        activateRow(p, idx);
    }
}

}  // namespace

const Skin kSkinMini = {"mini", W, H, 2, render, event};
