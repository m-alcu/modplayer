// Skin: term — a terminal music player in the cmus/moc mould: green-on-black
// monospace text, a '>' cursor, an ASCII progress bar and a status line.
// Every line is the built-in 8x8 font; a tap on a row activates it.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 320, H = 240;

constexpr Col BG    {8, 10, 8};
constexpr Col GREEN {96, 230, 120};
constexpr Col GDIM  {52, 120, 66};
constexpr Col WHITE {214, 230, 216};
constexpr Col INVBG {96, 230, 120};       // cursor row: green bg, black text

constexpr float CH = 12;                  // line height
constexpr float LIST_Y = CH + 4;
constexpr int   STATUS_LINES = 3;

std::string bar(float frac, int cells) {
    int lit = (int)(frac * cells + 0.5f);
    std::string s = "[";
    for (int i = 0; i < cells; ++i) s += i < lit ? '=' : '-';
    return s + "]";
}

void statusLines(SDL_Renderer* r) {
    float y0 = H - STATUS_LINES * CH;
    setCol(r, GDIM);
    fillR(r, 0, y0 - 2, W, 1);

    const Track* t = currentTrack();
    double dur = playback::duration(), pos = playback::position();
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;

    std::string l1 = t ? foldAscii(t->title +
                                   (t->meta.artist.empty()
                                        ? ""
                                        : " - " + t->meta.artist))
                       : "(not playing)";
    drawText(r, l1, 6, y0 + 2, 1, WHITE, W - 12);

    if (volMode) {
        char l2[96];
        std::snprintf(l2, sizeof l2, "vol %s %d%%",
                      bar(playback::volume(), 20).c_str(),
                      (int)std::lround(playback::volume() * 100));
        drawText(r, l2, 6, y0 + 2 + CH, 1, GREEN);
    } else {
        float frac = dur > 0 ? (float)(pos / dur) : 0;
        std::string l2 = bar(frac, 24) + " " + fmtTime(pos) + "/" +
                         fmtTime(dur);
        drawText(r, l2, 6, y0 + 2 + CH, 1, GREEN);
    }

    const char* state = !playback::active() ? "stopped"
                        : playback::paused() ? "paused" : "playing";
    char l3[96];
    std::snprintf(l3, sizeof l3, "%s | %d/%d | vol %d%%%s", state,
                  g.qpos + 1, (int)g.queue.size(),
                  (int)std::lround(playback::volume() * 100),
                  (SDL_GetTicks() / 500) % 2 ? " _" : "");
    drawText(r, l3, 6, y0 + 2 + 2 * CH, 1, GDIM);
}

void drawList(SDL_Renderer* r, Page& p) {
    std::string hdr = "modterm - " + foldAscii(p.title);
    if (g.nav.size() > 1) hdr += "  (q:back)";
    drawText(r, hdr, 6, 3, 1, GDIM);

    const int visible = (int)((H - LIST_Y - STATUS_LINES * CH - 4) / CH);
    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;

    for (int i = 0; i < visible; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        float y = LIST_Y + i * CH;
        bool sel = idx == p.cursor;
        std::string line = (sel ? "> " : "  ") + p.rows[idx].label +
                           (p.rows[idx].arrow ? "/" : "");
        if (sel) {
            setCol(r, INVBG);
            fillR(r, 0, y - 1, W, CH);
            drawText(r, line, 6, y + 1, 1, {8, 10, 8}, W - 12);
        } else {
            drawText(r, line, 6, y + 1, 1,
                     p.rows[idx].track >= 0 ? WHITE : GREEN, W - 12);
        }
    }
    if (p.rows.empty())
        drawText(r, "  (no songs found)", 6, LIST_Y + CH, 1, GDIM);
}

void drawNowPlaying(SDL_Renderer* r) {
    drawText(r, "modterm - now playing", 6, 3, 1, GDIM);
    const Track* t = currentTrack();
    float y = LIST_Y + CH;
    if (!t) {
        drawText(r, "  (nothing playing)", 6, y, 1, GDIM);
        return;
    }
    auto line = [&](const char* k, const std::string& v, Col c) {
        drawText(r, std::string("  ") + k, 6, y, 1, GDIM);
        drawText(r, v, 6 + 10 * ADV, y, 1, c, W - 12 - 10 * ADV);
        y += CH;
    };
    line("title:", t->title, WHITE);
    line("artist:", t->meta.artist.empty() ? Library::kUnknownArtist
                                           : t->meta.artist, GREEN);
    line("album:", t->meta.album.empty() ? Library::kUnknownAlbum
                                         : t->meta.album, GREEN);
    if (t->meta.year) line("year:", std::to_string(t->meta.year), GREEN);
    if (playback::sampleRate()) {
        char fmt[48];
        std::snprintf(fmt, sizeof fmt, "%d Hz %s", playback::sampleRate(),
                      playback::channels() == 1 ? "mono" : "stereo");
        line("format:", fmt, GDIM);
    }
    line("file:", t->path, GDIM);
}

void render(SDL_Renderer* r) {
    setCol(r, BG);
    fillR(r, 0, 0, W, H);
    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) drawNowPlaying(r);
    else drawList(r, p);
    statusLines(r);
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;
    Page& p = g.nav.back();

    float statusY = H - STATUS_LINES * CH;
    if (y >= statusY) {                    // status area: tap bar line seeks
        if (y >= statusY + CH && y < statusY + 2 * CH) {
            float f = std::clamp((x - 6) / (25 * ADV), 0.0f, 1.0f);
            playback::seekTo(f * playback::duration());
        } else {
            playPauseToggle();
        }
        return;
    }
    if (y < LIST_Y) {                      // header: back
        if (g.nav.size() > 1) goBack();
        return;
    }
    if (p.view == View::NOW_PLAYING) return;
    int idx = p.scroll + (int)((y - LIST_Y) / CH);
    if (idx >= 0 && idx < (int)p.rows.size()) {
        p.cursor = idx;
        activateRow(p, idx);
    }
}

}  // namespace

const Skin kSkinTerm = {"term", W, H, 2, render, event};
