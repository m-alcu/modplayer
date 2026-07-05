// Skin: cassette — Now Playing is an animated compact cassette: the reels
// spin while playing, the tape pack migrates from the left hub to the right
// as the track advances, and the label carries the tags. List pages are a
// plain dark list. Click the left/right reel for prev/next, the middle for
// play/pause, the counter strip at the bottom to seek.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 320, H = 240;

constexpr Col BG     {28, 26, 24};
constexpr Col SHELL  {56, 50, 44};        // cassette body
constexpr Col SHELL_D{38, 34, 30};
constexpr Col LABEL  {226, 219, 200};     // cream label
constexpr Col STRIPE {206, 96, 56};       // label stripe
constexpr Col TEXT   {232, 234, 240};
constexpr Col DIM    {150, 144, 136};
constexpr Col TAPE   {30, 22, 16};        // wound tape
constexpr Col HUB    {214, 214, 210};

constexpr SDL_FRect BODY{28, 34, 264, 164};
constexpr float REEL_LY = BODY.y + 96, REEL_LX = BODY.x + 78;
constexpr float REEL_RX = BODY.x + BODY.w - 78;
constexpr float HUB_R = 11, PACK_MAX = 25, PACK_MIN = 14;
constexpr SDL_FRect RC_CNT{28, 212, 264, 16};  // counter / seek strip

constexpr float ROW_H = 18, LIST_Y = 24;

void drawHubSpokes(SDL_Renderer* r, float cx, float cy, float ang) {
    for (int i = 0; i < 6; ++i) {
        float a = ang + i * (float)M_PI / 3;
        float x = cx + std::cos(a) * (HUB_R - 4);
        float y = cy + std::sin(a) * (HUB_R - 4);
        fillCircle(r, x, y, 2.2f, SHELL_D);
    }
}

void drawCassette(SDL_Renderer* r) {
    const Track* t = currentTrack();
    double dur = playback::duration(), pos = playback::position();
    float frac = dur > 0 ? (float)(pos / dur) : 0;

    // shell
    fillRoundRect(r, {BODY.x - 4, BODY.y - 4, BODY.w + 8, BODY.h + 8}, 10,
                  {16, 14, 12});
    fillRoundRect(r, BODY, 8, SHELL);
    // screws
    for (float sx : {BODY.x + 8, BODY.x + BODY.w - 8})
        for (float sy : {BODY.y + 8, BODY.y + BODY.h - 8})
            fillCircle(r, sx, sy, 2.5f, SHELL_D);

    // label with stripe + tags
    SDL_FRect lab{BODY.x + 18, BODY.y + 10, BODY.w - 36, 58};
    fillRoundRect(r, lab, 3, LABEL);
    setCol(r, STRIPE);
    fillR(r, lab.x, lab.y + 36, lab.w, 10);
    if (t) {
        drawText(r, t->title, lab.x + 8, lab.y + 7, 1, {40, 36, 32},
                 lab.w - 16, true);
        std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                    : t->meta.artist;
        drawText(r, artist, lab.x + 8, lab.y + 22, 1, {110, 100, 90},
                 lab.w - 16);
    } else {
        drawText(r, "no tape", lab.x + 8, lab.y + 14, 1, {110, 100, 90});
    }
    char side[16];
    std::snprintf(side, sizeof side, "A %d", g.qpos + 1);
    drawText(r, side, lab.x + 6, lab.y + 37, 1, LABEL, 0, true);

    // window with the reels
    SDL_FRect win{BODY.x + 44, BODY.y + 70, BODY.w - 88, 54};
    fillRoundRect(r, win, 4, {20, 17, 14});

    // tape packs: left shrinks, right grows
    float packL = PACK_MIN + (PACK_MAX - PACK_MIN) * (1 - frac);
    float packR = PACK_MIN + (PACK_MAX - PACK_MIN) * frac;
    bool spinning = playback::active() && !playback::paused();
    float ang = spinning ? (float)(SDL_GetTicks() % 100000) * 0.004f : 0.9f;

    fillCircle(r, REEL_LX, REEL_LY, packL, TAPE);
    fillCircle(r, REEL_RX, REEL_LY, packR, TAPE);
    // tape path across the bottom, thicker near the fuller reel
    setCol(r, TAPE);
    fillR(r, REEL_LX, BODY.y + BODY.h - 18, REEL_RX - REEL_LX, 3);
    fillR(r, REEL_LX - 2, REEL_LY, 3, BODY.y + BODY.h - 18 - REEL_LY);
    fillR(r, REEL_RX - 1, REEL_LY, 3, BODY.y + BODY.h - 18 - REEL_LY);

    // hubs (counter-rotating look comes free from the same angle)
    for (float cx : {REEL_LX, REEL_RX}) {
        fillCircle(r, cx, REEL_LY, HUB_R + 2, {60, 56, 50});
        fillCircle(r, cx, REEL_LY, HUB_R, HUB);
        drawHubSpokes(r, cx, REEL_LY, ang);
    }

    // counter strip: elapsed / seek bar / remaining
    setCol(r, {16, 14, 12});
    fillR(r, RC_CNT.x, RC_CNT.y, RC_CNT.w, RC_CNT.h);
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    if (volMode) {
        setCol(r, STRIPE);
        fillR(r, RC_CNT.x + 2, RC_CNT.y + 6, (RC_CNT.w - 4) * playback::volume(),
              4);
        drawTextCentered(r, "VOL", W / 2, RC_CNT.y + 4, 1, LABEL);
    } else if (t) {
        setCol(r, {60, 54, 48});
        fillR(r, RC_CNT.x + 44, RC_CNT.y + 6, RC_CNT.w - 88, 4);
        setCol(r, STRIPE);
        fillR(r, RC_CNT.x + 44, RC_CNT.y + 6, (RC_CNT.w - 88) * frac, 4);
        drawText(r, fmtTime(pos), RC_CNT.x + 2, RC_CNT.y + 4, 1, DIM);
        std::string rem = fmtTime(dur);
        drawText(r, rem, RC_CNT.x + RC_CNT.w - textW(rem, 1) - 2,
                 RC_CNT.y + 4, 1, DIM);
    } else {
        drawTextCentered(r, "press space to play", W / 2, RC_CNT.y + 4, 1,
                         DIM);
    }
}

void drawList(SDL_Renderer* r, Page& p) {
    setCol(r, {40, 36, 32});
    fillR(r, 0, 0, W, 20);
    if (g.nav.size() > 1) fillTriangle(r, 14, 4, 14, 16, 6, 10, DIM);
    drawTextCentered(r, p.title, W / 2, 6, 1, TEXT, true);

    const float listH = H - LIST_Y;
    const int visible = (int)(listH / ROW_H);
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
            setCol(r, STRIPE);
            fillR(r, 0, y, 3, ROW_H);
        }
        drawText(r, p.rows[idx].label, 12, y + 5, 1, sel ? TEXT : DIM, W - 32);
        if (p.rows[idx].arrow)
            drawTextRaw(r, ">", W - 14, y + 5, 1, sel ? TEXT : DIM);
    }
    if (p.rows.empty())
        drawTextCentered(r, "No songs found", W / 2, 100, 1, DIM);
}

void render(SDL_Renderer* r) {
    setCol(r, BG);
    fillR(r, 0, 0, W, H);
    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) drawCassette(r);
    else drawList(r, p);
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;
    Page& p = g.nav.back();

    if (p.view != View::NOW_PLAYING) {
        if (y < 20) {                        // header: back
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

    // Now Playing: counter strip seeks, reels skip, middle toggles
    if (y >= RC_CNT.y - 4) {
        float f = std::clamp((x - (RC_CNT.x + 44)) / (RC_CNT.w - 88), 0.0f,
                             1.0f);
        playback::seekTo(f * playback::duration());
        return;
    }
    float dl = std::hypot(x - REEL_LX, y - REEL_LY);
    float dr = std::hypot(x - REEL_RX, y - REEL_LY);
    if (dl < PACK_MAX + 8) prevTrack();
    else if (dr < PACK_MAX + 8) nextTrack();
    else if (y > BODY.y) playPauseToggle();
}

}  // namespace

const Skin kSkinCassette = {"cassette", W, H, 2, render, event};
