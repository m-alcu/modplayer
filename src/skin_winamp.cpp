// Skin: Winamp classic — beveled gray body, green LCD digits and marquee,
// live spectrum bars, transport buttons and a playlist pane. The playlist
// pane doubles as the library browser: it renders the same navigation pages
// as every other skin (Music > Artists / Albums / Songs / Years), and shows
// the play queue while on Now Playing.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 275, H = 240;

// palette
constexpr Col BODY {58, 61, 74};
constexpr Col LITE {104, 108, 126};
constexpr Col DARK {22, 24, 32};
constexpr Col LCD_BG {8, 10, 10};
constexpr Col GREEN {0, 230, 80};
constexpr Col GREEN_DIM {0, 120, 44};
constexpr Col PL_BG {12, 14, 16};
constexpr Col PL_SEL {30, 62, 130};
constexpr Col TITLE_TOP {88, 100, 156};
constexpr Col TITLE_BOT {40, 48, 92};

// layout
constexpr SDL_FRect RC_TIME {10, 22, 80, 32};
constexpr SDL_FRect RC_TITLE{98, 22, 167, 10};
constexpr SDL_FRect RC_SPEC {10, 62, 80, 34};
constexpr SDL_FRect RC_VOL  {126, 78, 139, 8};
constexpr SDL_FRect RC_SEEK {10, 104, 255, 10};
constexpr float BTN_Y = 120, BTN_W = 24, BTN_H = 16;
constexpr float PL_Y = 140, PL_HDR = 12, PL_ROW = 11;
constexpr int   PL_ROWS = 8;

void bevel(SDL_Renderer* r, SDL_FRect rc, Col base, bool sunken = false) {
    setCol(r, base);
    fillR(r, rc.x, rc.y, rc.w, rc.h);
    setCol(r, sunken ? DARK : LITE);
    fillR(r, rc.x, rc.y, rc.w, 1);
    fillR(r, rc.x, rc.y, 1, rc.h);
    setCol(r, sunken ? LITE : DARK);
    fillR(r, rc.x, rc.y + rc.h - 1, rc.w, 1);
    fillR(r, rc.x + rc.w - 1, rc.y, 1, rc.h);
}

// ------------------------------------------------------------ 7-seg digits

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

void drawLcdTime(SDL_Renderer* r) {
    bevel(r, RC_TIME, LCD_BG, true);
    double pos = playback::position();
    int m = (int)pos / 60, s = (int)pos % 60;
    if (m > 99) m = 99;
    const float dw = 13, dh = 24, y = RC_TIME.y + 4;
    float x = RC_TIME.x + 6;
    // blink the digits while paused, like the original
    if (playback::active() && playback::paused() && (SDL_GetTicks() / 500) % 2)
        return;
    drawSeg(r, x, y, dw, dh, kSeg[m / 10], GREEN); x += dw + 3;
    drawSeg(r, x, y, dw, dh, kSeg[m % 10], GREEN); x += dw + 3;
    setCol(r, GREEN);  // colon
    fillR(r, x + 1, y + 6, 3, 3);
    fillR(r, x + 1, y + 15, 3, 3);
    x += 7;
    drawSeg(r, x, y, dw, dh, kSeg[s / 10], GREEN); x += dw + 3;
    drawSeg(r, x, y, dw, dh, kSeg[s % 10], GREEN);
}

// ---------------------------------------------------------------- marquee

void drawMarquee(SDL_Renderer* r) {
    bevel(r, {RC_TITLE.x - 2, RC_TITLE.y - 2, RC_TITLE.w + 4, RC_TITLE.h + 4},
          LCD_BG, true);
    const Track* t = currentTrack();
    std::string s = t ? t->title +
                            (t->meta.artist.empty() ? "" : " - " + t->meta.artist)
                      : "modplayer :: winamp skin";
    s = foldAscii(s);
    const int fit = (int)(RC_TITLE.w / ADV);
    if ((int)s.size() > fit) {
        s += "  ***  ";
        size_t off = (SDL_GetTicks() / 220) % s.size();
        std::string roll = s.substr(off) + s.substr(0, off);
        s = roll.substr(0, fit);
    }
    drawTextRaw(r, s, RC_TITLE.x, RC_TITLE.y + 1, 1, GREEN);
}

// --------------------------------------------------------------- spectrum

float s_bars[16];
float s_peaks[16];

void updateSpectrum() {
    constexpr int N = 512;
    float smp[N];
    int got = playback::visSamples(smp, N);
    int rate = playback::sampleRate();
    for (int b = 0; b < 16; ++b) {
        float target = 0;
        if (got > 64 && rate > 0) {
            // Goertzel magnitude at 16 log-spaced frequencies.
            float f0 = 80, f1 = std::min(9000.0f, rate * 0.45f);
            float f = f0 * std::pow(f1 / f0, b / 15.0f);
            float w = 2 * (float)M_PI * f / rate;
            float coeff = 2 * std::cos(w), s1 = 0, s2 = 0;
            for (int i = 0; i < got; ++i) {
                float s0 = smp[i] + coeff * s1 - s2;
                s2 = s1; s1 = s0;
            }
            float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
            float mag = std::sqrt(std::max(0.0f, p)) / (got / 2.0f);
            target = std::clamp(std::sqrt(mag) * 1.8f, 0.0f, 1.0f);
        }
        s_bars[b] = std::max(target, s_bars[b] - 0.07f);
        s_peaks[b] = std::max(s_bars[b], s_peaks[b] - 0.012f);
    }
}

void drawSpectrum(SDL_Renderer* r) {
    bevel(r, {RC_SPEC.x - 2, RC_SPEC.y - 2, RC_SPEC.w + 4, RC_SPEC.h + 4},
          LCD_BG, true);
    const float bw = RC_SPEC.w / 16;
    for (int b = 0; b < 16; ++b) {
        float hpx = s_bars[b] * (RC_SPEC.h - 2);
        float x = RC_SPEC.x + b * bw;
        // classic look: green base, yellow middle, red tip — per 2px cell
        for (float yy = 0; yy < hpx; yy += 2) {
            float t = yy / (RC_SPEC.h - 2);
            Col c = t < 0.5f ? Col{0, 220, 60}
                  : t < 0.8f ? Col{230, 220, 0}
                             : Col{240, 40, 30};
            setCol(r, c);
            fillR(r, x, RC_SPEC.y + RC_SPEC.h - 1 - yy, bw - 1, 1.6f);
        }
        setCol(r, {200, 205, 215});  // falling peak dot
        fillR(r, x, RC_SPEC.y + RC_SPEC.h - 2 - s_peaks[b] * (RC_SPEC.h - 2),
              bw - 1, 1);
    }
}

// ----------------------------------------------------------------- sliders

void drawSlider(SDL_Renderer* r, SDL_FRect rc, float frac, Col fill) {
    bevel(r, rc, LCD_BG, true);
    setCol(r, fill);
    fillR(r, rc.x + 1, rc.y + 2, (rc.w - 2) * frac, rc.h - 4);
    float kx = rc.x + 1 + (rc.w - 8) * frac;
    bevel(r, {kx, rc.y - 1, 7, rc.h + 2}, {150, 154, 168});
}

// ---------------------------------------------------------------- buttons

// order: prev, play, pause, stop, next
SDL_FRect btnRect(int i) {
    return {10 + i * (BTN_W + 2), BTN_Y, BTN_W, BTN_H};
}

void drawButtons(SDL_Renderer* r) {
    Col ic{215, 218, 226};
    for (int i = 0; i < 5; ++i) {
        SDL_FRect b = btnRect(i);
        bevel(r, b, {78, 82, 98});
        float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        switch (i) {
            case 0:  // |<
                setCol(r, ic);
                fillR(r, cx - 6, cy - 4, 2, 8);
                fillTriangle(r, cx + 6, cy - 4, cx + 6, cy + 4, cx - 2, cy, ic);
                break;
            case 1:  // play
                fillTriangle(r, cx - 3, cy - 5, cx - 3, cy + 5, cx + 5, cy, ic);
                break;
            case 2:  // pause
                setCol(r, ic);
                fillR(r, cx - 4, cy - 4, 3, 8);
                fillR(r, cx + 1, cy - 4, 3, 8);
                break;
            case 3:  // stop
                setCol(r, ic);
                fillR(r, cx - 4, cy - 4, 8, 8);
                break;
            case 4:  // >|
                fillTriangle(r, cx - 6, cy - 4, cx - 6, cy + 4, cx + 2, cy, ic);
                setCol(r, ic);
                fillR(r, cx + 4, cy - 4, 2, 8);
                break;
        }
    }
}

// ---------------------------------------------------------------- playlist

// The playlist pane shows the play queue while on Now Playing, otherwise the
// current navigation page.
bool showingQueue() { return g.nav.back().view == View::NOW_PLAYING; }

void drawPlaylist(SDL_Renderer* r) {
    Page& p = g.nav.back();
    vGradient(r, {0, PL_Y, W, PL_HDR}, TITLE_TOP, TITLE_BOT);
    // back button in the header
    bevel(r, {3, PL_Y + 2, 14, PL_HDR - 4}, {78, 82, 98});
    drawTextRaw(r, "<", 7, PL_Y + 2, 1, {220, 224, 232});
    drawTextCentered(r, p.title, W / 2, PL_Y + 2, 1, {225, 228, 236}, true);

    setCol(r, PL_BG);
    fillR(r, 0, PL_Y + PL_HDR, W, H - PL_Y - PL_HDR);

    float y0 = PL_Y + PL_HDR + 1;
    if (showingQueue()) {
        int scroll = std::clamp(g.qpos - PL_ROWS / 2, 0,
                                std::max(0, (int)g.queue.size() - PL_ROWS));
        for (int i = 0; i < PL_ROWS; ++i) {
            int idx = scroll + i;
            if (idx >= (int)g.queue.size()) break;
            const Track& t = g.lib.tracks()[g.queue[idx]];
            bool cur = idx == g.qpos;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%d.", idx + 1);
            Col c = cur ? Col{255, 255, 255} : GREEN;
            if (cur) { setCol(r, PL_SEL); fillR(r, 0, y0 + i * PL_ROW, W, PL_ROW); }
            drawTextRaw(r, buf, 4, y0 + i * PL_ROW + 1, 1, c);
            drawText(r, t.title, 30, y0 + i * PL_ROW + 1, 1, c, W - 90);
            std::string d = fmtTime(cur ? playback::duration() : 0);
            if (cur)
                drawTextRaw(r, d, W - 8 - textW(d, 1), y0 + i * PL_ROW + 1, 1, c);
        }
    } else {
        const int visible = PL_ROWS;
        if (p.cursor < p.scroll) p.scroll = p.cursor;
        if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;
        for (int i = 0; i < visible; ++i) {
            int idx = p.scroll + i;
            if (idx >= (int)p.rows.size()) break;
            bool sel = idx == p.cursor;
            if (sel) { setCol(r, PL_SEL); fillR(r, 0, y0 + i * PL_ROW, W, PL_ROW); }
            Col c = sel ? Col{255, 255, 255} : GREEN;
            drawText(r, p.rows[idx].label, 6, y0 + i * PL_ROW + 1, 1, c, W - 24);
            if (p.rows[idx].arrow)
                drawTextRaw(r, ">", W - 12, y0 + i * PL_ROW + 1, 1, c);
        }
        if (p.rows.empty())
            drawTextCentered(r, "No songs found", W / 2, y0 + 30, 1, GREEN_DIM);
        if ((int)p.rows.size() > visible) {
            float trackH = PL_ROWS * PL_ROW;
            float barH = std::max(8.0f, trackH * visible / p.rows.size());
            float t = (float)p.scroll / (p.rows.size() - visible);
            setCol(r, {40, 44, 52});
            fillR(r, W - 4, y0, 3, trackH);
            setCol(r, {120, 126, 140});
            fillR(r, W - 4, y0 + t * (trackH - barH), 3, barH);
        }
    }
}

// ----------------------------------------------------------------- render

void render(SDL_Renderer* r) {
    setCol(r, BODY);
    fillR(r, 0, 0, W, H);
    bevel(r, {0, 0, W, PL_Y}, BODY);

    // title bar
    vGradient(r, {0, 0, W, 14}, TITLE_TOP, TITLE_BOT);
    drawTextCentered(r, "* MODPLAYER *", W / 2, 3, 1, {225, 228, 236}, true);
    setCol(r, {140, 148, 180});
    for (float x = 6; x < W / 2 - 56; x += 4) { fillR(r, x, 5, 2, 1); fillR(r, x, 8, 2, 1); }
    for (float x = W / 2 + 56; x < W - 6; x += 4) { fillR(r, x, 5, 2, 1); fillR(r, x, 8, 2, 1); }

    drawLcdTime(r);
    drawMarquee(r);

    // info line: kbps-style readout
    const Track* t = currentTrack();
    char info[64];
    if (t && playback::sampleRate() > 0)
        std::snprintf(info, sizeof info, "%dKHZ %s", playback::sampleRate() / 1000,
                      playback::channels() >= 2 ? "STEREO" : "MONO");
    else
        std::snprintf(info, sizeof info, "-- KHZ --");
    drawTextRaw(r, info, RC_TITLE.x, 40, 1, GREEN_DIM);
    if (t) {
        std::string al = t->meta.album.empty() ? "" : t->meta.album;
        if (t->meta.year) al += (al.empty() ? "" : " ") +
                                ("(" + std::to_string(t->meta.year) + ")");
        drawText(r, al, RC_TITLE.x, 52, 1, GREEN_DIM, RC_TITLE.w);
    }

    updateSpectrum();
    drawSpectrum(r);

    drawTextRaw(r, "VOL", RC_VOL.x - 26, RC_VOL.y, 1, {170, 175, 188});
    drawSlider(r, RC_VOL, playback::volume(), GREEN_DIM);

    double dur = playback::duration();
    float frac = dur > 0 ? (float)(playback::position() / dur) : 0;
    drawSlider(r, RC_SEEK, frac, {120, 126, 148});
    // elapsed/total, right-aligned above the seek bar (the LCD shows elapsed)
    std::string dtxt = fmtTime(playback::position()) + " / " + fmtTime(dur);
    drawTextRaw(r, dtxt, RC_SEEK.x + RC_SEEK.w - textW(dtxt, 1),
                RC_SEEK.y - 12, 1, {170, 175, 188});

    drawButtons(r);
    drawPlaylist(r);
}

// ------------------------------------------------------------------ input

int s_drag = 0;  // 1 = seek bar, 2 = volume

bool inRect(float x, float y, SDL_FRect rc) {
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

void applySlider(float x) {
    if (s_drag == 1) {
        float f = std::clamp((x - RC_SEEK.x) / RC_SEEK.w, 0.0f, 1.0f);
        playback::seekTo(f * playback::duration());
    } else if (s_drag == 2) {
        playback::setVolume(std::clamp((x - RC_VOL.x) / RC_VOL.w, 0.0f, 1.0f));
    }
}

void event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            float x = e.button.x, y = e.button.y;
            if (inRect(x, y, RC_SEEK)) { s_drag = 1; applySlider(x); break; }
            if (inRect(x, y, {RC_VOL.x, RC_VOL.y - 2, RC_VOL.w, RC_VOL.h + 4})) {
                s_drag = 2; applySlider(x); break;
            }
            for (int i = 0; i < 5; ++i)
                if (inRect(x, y, btnRect(i))) {
                    if (i == 0) prevTrack();
                    else if (i == 1) playOrResume();
                    else if (i == 2) { if (playback::active()) playback::setPaused(true); }
                    else if (i == 3) { playback::stop(); }
                    else nextTrack();
                    return;
                }
            if (inRect(x, y, {3, PL_Y + 2, 14, PL_HDR - 4})) { goBack(); break; }
            if (y >= PL_Y + PL_HDR) {
                int row = (int)((y - PL_Y - PL_HDR - 1) / PL_ROW);
                Page& p = g.nav.back();
                if (showingQueue()) break;  // queue view is read-only
                int idx = p.scroll + row;
                if (idx >= 0 && idx < (int)p.rows.size()) {
                    p.cursor = idx;
                    activateRow(p, idx);
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (s_drag) applySlider(e.motion.x);
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            s_drag = 0;
            break;
        default: break;
    }
}

}  // namespace

const Skin kSkinWinamp = {"winamp", W, H, 2, render, event};
