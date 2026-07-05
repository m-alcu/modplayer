// Skin: car — a 90s CD head unit: wide black faceplate, glowing amber VFD
// with one big scrolling text line (the selected row while browsing, the
// track while playing), a 7-segment clock, and a row of chunky buttons.
// Landscape and shallow, so it also suits automotive-ish small displays.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 360, H = 150;

constexpr Col FACE   {26, 26, 28};
constexpr Col FACE_L {52, 52, 56};
constexpr Col VFD_BG {12, 20, 18};
constexpr Col AMBER  {255, 174, 40};
constexpr Col AMBER_D{120, 82, 22};
constexpr Col BTN    {38, 38, 42};
constexpr Col BTN_TX {214, 214, 220};

constexpr SDL_FRect VFD{12, 12, 336, 74};
constexpr float BTN_Y = 96, BTN_H = 40;

// segment bits: 1 top, 2 tr, 4 br, 8 bottom, 16 bl, 32 tl, 64 middle
constexpr int kSeg[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};

void drawSeg(SDL_Renderer* r, float x, float y, float w, float h, int mask,
             Col c) {
    const float t = 3;
    setCol(r, c);
    if (mask & 1)  fillR(r, x + 1, y, w - 2, t);
    if (mask & 2)  fillR(r, x + w - t, y + 1, t, h / 2 - 1);
    if (mask & 4)  fillR(r, x + w - t, y + h / 2, t, h / 2 - 1);
    if (mask & 8)  fillR(r, x + 1, y + h - t, w - 2, t);
    if (mask & 16) fillR(r, x, y + h / 2, t, h / 2 - 1);
    if (mask & 32) fillR(r, x, y + 1, t, h / 2 - 1);
    if (mask & 64) fillR(r, x + 1, y + h / 2 - t / 2, w - 2, t);
}

// the 9 faceplate buttons
const char* kBtns[] = {"SRC", "/\\", "\\/", "SEL", "|<<", ">||", ">>|",
                       "V-", "V+"};
constexpr int NBTN = 9;

SDL_FRect btnRect(int i) {
    float w = (float)W / NBTN;
    return {i * w + 2, BTN_Y, w - 4, BTN_H};
}

void drawVfd(SDL_Renderer* r) {
    Page& p = g.nav.back();
    const Track* t = currentTrack();
    bool nowPlaying = p.view == View::NOW_PLAYING;

    fillRoundRect(r, {VFD.x - 3, VFD.y - 3, VFD.w + 6, VFD.h + 6}, 5,
                  {6, 8, 8});
    setCol(r, VFD_BG);
    fillR(r, VFD.x, VFD.y, VFD.w, VFD.h);

    // status line: mode + indicators
    std::string mode = nowPlaying ? "NOW PLAYING" : foldAscii(p.title);
    for (char& c : mode)
        if (c >= 'a' && c <= 'z') c -= 32;
    drawTextRaw(r, mode.substr(0, 24), VFD.x + 8, VFD.y + 6, 1, AMBER_D);
    if (playback::active()) {
        float ix = VFD.x + VFD.w - 60, iy = VFD.y + 6;
        if (playback::paused()) {
            setCol(r, AMBER);
            fillR(r, ix, iy, 3, 8);
            fillR(r, ix + 5, iy, 3, 8);
        } else if ((SDL_GetTicks() / 600) % 2 == 0 ||
                   !playback::paused()) {
            fillTriangle(r, ix, iy, ix, iy + 8, ix + 7, iy + 4, AMBER);
        }
        char tno[16];
        std::snprintf(tno, sizeof tno, "T%02d", g.qpos + 1);
        drawTextRaw(r, tno, VFD.x + VFD.w - 44, VFD.y + 6, 1, AMBER_D);
    }

    // main line: big scrolling text
    std::string s;
    if (nowPlaying && t) {
        s = t->title;
        if (!t->meta.artist.empty()) s += " - " + t->meta.artist;
    } else if (!p.rows.empty()) {
        s = p.rows[p.cursor].label;
        if (p.rows[p.cursor].arrow) s += " >";
    } else {
        s = "NO DISC";
    }
    s = foldAscii(s);
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c -= 32;
    const float mainW = VFD.w - 110;
    const int fit = (int)(mainW / (ADV * 2));
    if ((int)s.size() > fit) {
        s += "   *   ";
        size_t off = (SDL_GetTicks() / 260) % s.size();
        s = (s.substr(off) + s.substr(0, off)).substr(0, fit);
    }
    drawTextRaw(r, s, VFD.x + 8, VFD.y + 26, 2, AMBER);

    // browse cursor position, small under the main line
    if (!nowPlaying && !p.rows.empty()) {
        char posbuf[24];
        std::snprintf(posbuf, sizeof posbuf, "%d/%d", p.cursor + 1,
                      (int)p.rows.size());
        drawTextRaw(r, posbuf, VFD.x + 8, VFD.y + 52, 1, AMBER_D);
    }

    // 7-seg clock: track position (or volume while the overlay is up)
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    float dx = VFD.x + VFD.w - 96, dy = VFD.y + 26, dw = 16, dh = 28;
    if (volMode) {
        int v = (int)std::lround(playback::volume() * 99);
        drawTextRaw(r, "VOL", dx, dy + 8, 1, AMBER_D);
        drawSeg(r, dx + 34, dy, dw, dh, kSeg[v / 10], AMBER);
        drawSeg(r, dx + 34 + dw + 4, dy, dw, dh, kSeg[v % 10], AMBER);
    } else {
        double pos = playback::position();
        int m = std::min(99, (int)pos / 60), sec = (int)pos % 60;
        drawSeg(r, dx, dy, dw, dh, kSeg[m / 10], AMBER);
        drawSeg(r, dx + dw + 4, dy, dw, dh, kSeg[m % 10], AMBER);
        setCol(r, AMBER);
        fillR(r, dx + 2 * dw + 6, dy + 7, 3, 3);
        fillR(r, dx + 2 * dw + 6, dy + 18, 3, 3);
        drawSeg(r, dx + 2 * dw + 12, dy, dw, dh, kSeg[sec / 10], AMBER);
        drawSeg(r, dx + 2 * dw + 12 + dw + 4, dy, dw, dh, kSeg[sec % 10],
                AMBER);
    }

    // progress ticks along the VFD bottom
    if (t) {
        double dur = playback::duration();
        float frac = dur > 0 ? (float)(playback::position() / dur) : 0;
        int lit = (int)(frac * 20 + 0.5f);
        for (int i = 0; i < 20; ++i) {
            setCol(r, i < lit ? AMBER : Col{40, 46, 42});
            fillR(r, VFD.x + 8 + i * 12, VFD.y + VFD.h - 8, 8, 3);
        }
    }
}

void render(SDL_Renderer* r) {
    setCol(r, FACE);
    fillR(r, 0, 0, W, H);
    setCol(r, FACE_L);
    fillR(r, 0, 0, W, 2);
    fillR(r, 0, H - 2, W, 2);

    drawVfd(r);

    for (int i = 0; i < NBTN; ++i) {
        SDL_FRect b = btnRect(i);
        fillRoundRect(r, b, 4, BTN);
        setCol(r, FACE_L);
        fillR(r, b.x, b.y, b.w, 1);
        drawTextCentered(r, kBtns[i], b.x + b.w / 2, b.y + b.h / 2 - 4, 1,
                         BTN_TX, true);
    }
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;
    if (y < BTN_Y) {                      // VFD tap: seek along the ticks
        Page& p = g.nav.back();
        if (p.view == View::NOW_PLAYING && y > VFD.y + VFD.h - 16) {
            float f = std::clamp((x - VFD.x - 8) / 240.0f, 0.0f, 1.0f);
            playback::seekTo(f * playback::duration());
        }
        return;
    }
    int i = std::clamp((int)(x / ((float)W / NBTN)), 0, NBTN - 1);
    switch (i) {
        case 0: goBack(); break;
        case 1: scrollStep(-1); break;
        case 2: scrollStep(+1); break;
        case 3: selectPress(); break;
        case 4: prevTrack(); break;
        case 5: playPauseToggle(); break;
        case 6: nextTrack(); break;
        case 7: playback::setVolume(playback::volume() - 0.05f);
                g.volOverlayUntil = SDL_GetTicks() + 1200; break;
        case 8: playback::setVolume(playback::volume() + 0.05f);
                g.volOverlayUntil = SDL_GetTicks() + 1200; break;
    }
}

}  // namespace

const Skin kSkinCar = {"car", W, H, 3, render, event};
