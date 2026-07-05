// Skin: Zune — Microsoft Metro look: flat black, oversized lowercase
// typography that bleeds off the right edge, magenta accent, thin progress
// hairline. Tap a row to activate it, tap the big header to go back.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 272, H = 480;

constexpr Col BG    {0, 0, 0};
constexpr Col TEXT  {242, 242, 242};
constexpr Col DIM   {110, 110, 110};
constexpr Col FAINT {60, 60, 60};
constexpr Col PINK  {236, 0, 140};        // Zune magenta

constexpr float HDR_H = 64;               // big lowercase page title
constexpr float ROW_H = 30;               // scale-2 rows
constexpr float LIST_Y = HDR_H + 8;

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

void drawHeader(SDL_Renderer* r, Page& p) {
    // tiny breadcrumb, then the big title (deliberately allowed to overflow
    // the right edge, Metro style)
    if (g.nav.size() > 1)
        drawText(r, "< " + lower(g.nav[g.nav.size() - 2].title), 12, 10, 1,
                 DIM);
    else
        drawText(r, "zune", 12, 10, 1, PINK, 0, true);
    std::string title = lower(p.view == View::NOW_PLAYING ? "now playing"
                                                          : p.title);
    drawText(r, title, 10, 26, 3, TEXT, 0, true);
}

void drawList(SDL_Renderer* r, Page& p) {
    const int visible = (int)((H - LIST_Y - 20) / ROW_H);
    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;

    for (int i = 0; i < visible; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        float y = LIST_Y + i * ROW_H;
        bool sel = idx == p.cursor;
        drawText(r, lower(p.rows[idx].label), 12, y + 6, 2,
                 sel ? PINK : TEXT, (float)W, sel);
    }

    if (p.rows.empty()) {
        drawText(r, "no songs found", 12, LIST_Y + 20, 2, DIM);
        drawText(r, "modplayer <mods folder>", 12, LIST_Y + 50, 1, FAINT);
    }

    // hairline scroll hint
    if ((int)p.rows.size() > visible) {
        float trackH = H - LIST_Y - 28;
        float barH = std::max(18.0f, trackH * visible / p.rows.size());
        float t = (float)p.scroll / (p.rows.size() - visible);
        setCol(r, FAINT);
        fillR(r, W - 3, LIST_Y, 1, trackH);
        setCol(r, PINK);
        fillR(r, W - 3, LIST_Y + t * (trackH - barH), 1, barH);
    }
}

void drawNowPlaying(SDL_Renderer* r) {
    const Track* t = currentTrack();
    if (!t) {
        drawText(r, "nothing playing", 12, LIST_Y + 20, 2, DIM);
        return;
    }

    // full-bleed square art
    SDL_FRect box{12, HDR_H + 12, (float)W - 24, (float)W - 24};
    if (g.art) {
        float tw, th;
        SDL_GetTextureSize(g.art, &tw, &th);
        float sc = std::min(box.w / tw, box.h / th);
        SDL_FRect dst{box.x + (box.w - tw * sc) / 2,
                      box.y + (box.h - th * sc) / 2, tw * sc, th * sc};
        SDL_RenderTexture(r, g.art, nullptr, &dst);
    } else {
        drawNoArt(r, box, {26, 26, 26}, {12, 12, 12}, {70, 70, 70});
    }

    float y = box.y + box.h + 14;
    drawText(r, lower(t->title), 12, y, 2, TEXT, W - 16, true);
    std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                : t->meta.artist;
    std::string album = t->meta.album.empty() ? Library::kUnknownAlbum
                                              : t->meta.album;
    drawText(r, lower(artist), 12, y + 24, 1, PINK, W - 24);
    drawText(r, lower(album), 12, y + 40, 1, DIM, W - 24);

    // hairline progress / volume
    float by = H - 40;
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    setCol(r, FAINT);
    fillR(r, 12, by, W - 24, 2);
    if (volMode) {
        setCol(r, PINK);
        fillR(r, 12, by, (W - 24) * playback::volume(), 2);
        drawText(r, "volume", 12, by + 8, 1, DIM);
    } else {
        double dur = playback::duration(), pos = playback::position();
        float frac = dur > 0 ? (float)(pos / dur) : 0;
        setCol(r, PINK);
        fillR(r, 12, by, (W - 24) * frac, 2);
        drawText(r, fmtTime(pos), 12, by + 8, 1, DIM);
        std::string rem = fmtTime(dur);
        drawText(r, rem, W - 12 - textW(rem, 1), by + 8, 1, DIM);
    }

    char idxbuf[32];
    std::snprintf(idxbuf, sizeof idxbuf, "%d / %d", g.qpos + 1,
                  (int)g.queue.size());
    drawText(r, idxbuf, 12, by + 22, 1, FAINT);
}

void render(SDL_Renderer* r) {
    setCol(r, BG);
    fillR(r, 0, 0, W, H);
    Page& p = g.nav.back();
    drawHeader(r, p);
    if (p.view == View::NOW_PLAYING) drawNowPlaying(r);
    else drawList(r, p);
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;
    Page& p = g.nav.back();

    if (y < HDR_H) {                       // header tap: back
        if (g.nav.size() > 1) goBack();
        return;
    }
    if (p.view == View::NOW_PLAYING) {
        float by = H - 40;
        if (y > by - 10 && y < by + 14) {  // hairline: seek
            float f = std::clamp((x - 12) / (W - 24), 0.0f, 1.0f);
            playback::seekTo(f * playback::duration());
        } else if (y > HDR_H + 12 && y < HDR_H + 12 + (W - 24)) {
            playPauseToggle();             // tap the art
        }
        return;
    }
    int idx = p.scroll + (int)((y - LIST_Y) / ROW_H);
    if (idx >= 0 && idx < (int)p.rows.size()) {
        p.cursor = idx;
        activateRow(p, idx);
    }
}

}  // namespace

const Skin kSkinZune = {"zune", W, H, 1, render, event};
