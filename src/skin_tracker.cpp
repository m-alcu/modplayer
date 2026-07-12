// Skin: tracker — FastTracker II / Scream Tracker 3 mould: gray-blue beveled
// panels, sunken readouts (order/pattern/row/speed/BPM), one oscilloscope per
// module channel, and the pattern editor with note rows scrolling under a
// fixed center bar. The right panel is the library browser; while a module
// plays it lists the instrument/sample names, demoscene greetings included.
//
// Pattern text and per-channel scope signals come from playback's tracker
// data (captured via libopenmpt at load). On the pocketmod fallback the
// scopes collapse to one mixed-signal scope and the pattern pane says why.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 640, H = 400;

// palette (FT2 default scheme, eyeballed)
constexpr Col BODY  {92, 96, 140};
constexpr Col LITE  {156, 161, 205};
constexpr Col DARK  {34, 36, 64};
constexpr Col INSET {6, 8, 14};         // sunken black displays
constexpr Col LABEL {212, 216, 242};    // panel captions
constexpr Col DIM   {150, 154, 190};
constexpr Col VALUE {120, 220, 255};    // readout digits (cyan)
constexpr Col ACCENT{250, 220, 90};     // song title (gold)
constexpr Col SCOPE {110, 255, 140};    // waveform
constexpr Col SCOPE_DIM {40, 90, 55};
constexpr Col GRID  {26, 28, 48};
constexpr Col SEL_BAR {70, 78, 130};

// pattern view colors
constexpr Col PAT_BG   {8, 9, 16};
constexpr Col PAT_BEAT {20, 22, 40};    // every 4th row tint
constexpr Col PAT_BAR  {62, 68, 118};   // center (playing) row
constexpr Col ROWNUM   {110, 160, 220};
constexpr Col F_NOTE   {235, 238, 255};
constexpr Col F_INST   {120, 220, 255};
constexpr Col F_VOL    {130, 240, 150};
constexpr Col F_FX     {250, 210, 110};
constexpr Col F_EMPTY  {72, 76, 116};

// layout
constexpr SDL_FRect PANEL_INFO {2, 2, 206, 92};
constexpr SDL_FRect PANEL_CTRL {210, 2, 168, 92};
constexpr SDL_FRect PANEL_LIST {380, 2, 258, 92};
constexpr SDL_FRect PANEL_SCOPE{2, 96, 636, 80};
constexpr SDL_FRect PANEL_PAT  {2, 178, 636, 220};
constexpr SDL_FRect RC_SEEK {218, 56, 152, 10};
constexpr SDL_FRect RC_VOL  {244, 80, 126, 8};
constexpr SDL_FRect RC_BACK {383, 4, 12, 10};
constexpr float BTN_X = 216, BTN_Y = 30, BTN_W = 28, BTN_H = 16;
constexpr float LIST_Y = 16, LIST_ROW = 9;
constexpr int   LIST_ROWS = 8;

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

// Sunken black readout box with cyan text.
void inset(SDL_Renderer* r, SDL_FRect rc, const std::string& txt) {
    bevel(r, rc, INSET, true);
    drawTextRaw(r, txt, rc.x + 3, rc.y + 2, 1, VALUE);
}

bool inRect(float x, float y, SDL_FRect rc) {
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

// ------------------------------------------------------------- info panel

void drawInfo(SDL_Renderer* r) {
    bevel(r, PANEL_INFO, BODY);
    const Track* t = currentTrack();

    std::string song = playback::trackerTitle();
    if (song.empty() && t) song = t->title;
    if (song.empty()) song = "(no module)";
    drawTextRaw(r, "SONG", 8, 8, 1, DIM);
    drawText(r, song, 44, 8, 1, ACCENT, PANEL_INFO.w - 50, true);
    drawTextRaw(r, "FILE", 8, 20, 1, DIM);
    drawText(r, t ? t->title : "-", 44, 20, 1, LABEL, PANEL_INFO.w - 50);

    playback::TrackerState st{};
    bool has = playback::trackerState(st);
    char b[24];
    auto num = [&](const char* fmt, int a, int v = -1) {
        if (!has) return std::string("--");
        std::snprintf(b, sizeof b, fmt, a, v);
        return std::string(b);
    };
    struct { const char* label; float x, y, w; std::string v; } boxes[] = {
        {"ORD", 8,   34, 44, num("%02d/%02d", st.order,
                                 playback::trackerOrders())},
        {"PAT", 104, 34, 44, num("%02d", st.pattern)},
        {"ROW", 8,   50, 44, num("%02d/%02d", st.row, st.numRows)},
        {"CHN", 104, 50, 44, has ? std::to_string(playback::trackerChannels())
                                 : "--"},
        {"SPD", 8,   66, 44, num("%d", st.speed)},
        {"BPM", 104, 66, 44, num("%d", st.tempo)},
    };
    for (const auto& bx : boxes) {
        drawTextRaw(r, bx.label, bx.x, bx.y + 2, 1, DIM);
        inset(r, {bx.x + 26, bx.y, bx.w, 12}, bx.v);
    }
    drawTextRaw(r, "TIME", 8, 82, 1, DIM);
    drawTextRaw(r, fmtTime(playback::position()) + " / " +
                       fmtTime(playback::duration()),
                44, 82, 1, VALUE);
}

// ---------------------------------------------------------- control panel

SDL_FRect btnRect(int i) { return {BTN_X + i * (BTN_W + 3), BTN_Y, BTN_W, BTN_H}; }

void drawCtrl(SDL_Renderer* r) {
    bevel(r, PANEL_CTRL, BODY);
    drawTextCentered(r, "MODPLAYER", PANEL_CTRL.x + PANEL_CTRL.w / 2, 7, 1,
                     ACCENT, true);
    drawTextCentered(r, "fasttracker skin", PANEL_CTRL.x + PANEL_CTRL.w / 2,
                     17, 1, DIM);

    Col ic{225, 228, 246};
    for (int i = 0; i < 5; ++i) {  // prev play pause stop next
        SDL_FRect bxr = btnRect(i);
        bevel(r, bxr, {110, 114, 160});
        float cx = bxr.x + bxr.w / 2, cy = bxr.y + bxr.h / 2;
        switch (i) {
            case 0:
                setCol(r, ic);
                fillR(r, cx - 6, cy - 4, 2, 8);
                fillTriangle(r, cx + 6, cy - 4, cx + 6, cy + 4, cx - 2, cy, ic);
                break;
            case 1:
                fillTriangle(r, cx - 3, cy - 5, cx - 3, cy + 5, cx + 5, cy, ic);
                break;
            case 2:
                setCol(r, ic);
                fillR(r, cx - 4, cy - 4, 3, 8);
                fillR(r, cx + 1, cy - 4, 3, 8);
                break;
            case 3:
                setCol(r, ic);
                fillR(r, cx - 4, cy - 4, 8, 8);
                break;
            case 4:
                fillTriangle(r, cx - 6, cy - 4, cx - 6, cy + 4, cx + 2, cy, ic);
                setCol(r, ic);
                fillR(r, cx + 4, cy - 4, 2, 8);
                break;
        }
    }

    double dur = playback::duration();
    float frac = dur > 0 ? (float)(playback::position() / dur) : 0;
    bevel(r, RC_SEEK, INSET, true);
    setCol(r, VALUE);
    fillR(r, RC_SEEK.x + 1, RC_SEEK.y + 2, (RC_SEEK.w - 2) * frac,
          RC_SEEK.h - 4);

    drawTextRaw(r, "VOL", RC_VOL.x - 26, RC_VOL.y, 1, DIM);
    bevel(r, RC_VOL, INSET, true);
    setCol(r, SCOPE);
    fillR(r, RC_VOL.x + 1, RC_VOL.y + 2, (RC_VOL.w - 2) * playback::volume(),
          RC_VOL.h - 4);
}

// ------------------------------------------- browser / instruments panel

bool showingInstruments() {
    return g.nav.back().view == View::NOW_PLAYING &&
           !playback::trackerInstruments().empty();
}

void drawList(SDL_Renderer* r) {
    bevel(r, PANEL_LIST, BODY);
    setCol(r, DARK);
    fillR(r, PANEL_LIST.x + 2, PANEL_LIST.y + 2, PANEL_LIST.w - 4, 11);

    Page& p = g.nav.back();
    bool instr = showingInstruments();
    drawTextCentered(r, instr ? "Instruments" : p.title,
                     PANEL_LIST.x + PANEL_LIST.w / 2, PANEL_LIST.y + 4, 1,
                     LABEL, true);
    if (g.nav.size() > 1) {
        bevel(r, RC_BACK, {110, 114, 160});
        drawTextRaw(r, "<", RC_BACK.x + 3, RC_BACK.y + 1, 1, LABEL);
    }

    float y0 = PANEL_LIST.y + LIST_Y;
    float x0 = PANEL_LIST.x + 6;
    if (instr) {
        const std::vector<std::string>& ins = playback::trackerInstruments();
        int pages = ((int)ins.size() + LIST_ROWS - 1) / LIST_ROWS;
        int page = pages > 1 ? (int)((SDL_GetTicks() / 4000) % pages) : 0;
        for (int i = 0; i < LIST_ROWS; ++i) {
            int idx = page * LIST_ROWS + i;
            if (idx >= (int)ins.size()) break;
            char n[16];
            std::snprintf(n, sizeof n, "%02X", idx + 1);
            drawTextRaw(r, n, x0, y0 + i * LIST_ROW, 1, VALUE);
            drawText(r, ins[idx], x0 + 22, y0 + i * LIST_ROW, 1, LABEL,
                     PANEL_LIST.w - 36);
        }
        return;
    }

    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + LIST_ROWS) p.scroll = p.cursor - LIST_ROWS + 1;
    for (int i = 0; i < LIST_ROWS; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        bool sel = idx == p.cursor;
        if (sel) {
            setCol(r, SEL_BAR);
            fillR(r, PANEL_LIST.x + 2, y0 + i * LIST_ROW - 1,
                  PANEL_LIST.w - 4, LIST_ROW);
        }
        Col c = sel ? C_WHITE : LABEL;
        drawText(r, p.rows[idx].label, x0, y0 + i * LIST_ROW, 1, c,
                 PANEL_LIST.w - 26);
        if (p.rows[idx].arrow)
            drawTextRaw(r, ">", PANEL_LIST.x + PANEL_LIST.w - 12,
                        y0 + i * LIST_ROW, 1, c);
    }
    if (p.rows.empty() && p.view == View::LIST)
        drawTextCentered(r, "No songs found", PANEL_LIST.x + PANEL_LIST.w / 2,
                         y0 + 26, 1, DIM);
    if ((int)p.rows.size() > LIST_ROWS) {
        float trackH = LIST_ROWS * LIST_ROW;
        float barH = std::max(6.0f, trackH * LIST_ROWS / p.rows.size());
        float t = (float)p.scroll / (p.rows.size() - LIST_ROWS);
        setCol(r, DARK);
        fillR(r, PANEL_LIST.x + PANEL_LIST.w - 5, y0, 3, trackH);
        setCol(r, LITE);
        fillR(r, PANEL_LIST.x + PANEL_LIST.w - 5, y0 + t * (trackH - barH), 3,
              barH);
    }
}

// ------------------------------------------------------------------ scopes

// One waveform per cell: ~35ms of the channel's signal from the playhead.
void drawWave(SDL_Renderer* r, SDL_FRect cell, const float* smp, int n) {
    float cy = cell.y + cell.h / 2;
    if (n < 2) {  // silent / no data: flat line
        setCol(r, SCOPE_DIM);
        fillR(r, cell.x, cy, cell.w, 1);
        return;
    }
    float half = cell.h / 2 - 2;
    setCol(r, SCOPE);
    float prev = cy;
    for (int x = 0; x < (int)cell.w; ++x) {
        float s = smp[(int)((float)x * n / cell.w)];
        float y = cy - std::clamp(s, -1.0f, 1.0f) * half;
        float a = std::min(prev, y), b = std::max(prev, y);
        fillR(r, cell.x + x, a, 1, b - a + 1);
        prev = y;
    }
}

void drawScopes(SDL_Renderer* r) {
    bevel(r, PANEL_SCOPE, INSET, true);
    SDL_FRect in{PANEL_SCOPE.x + 1, PANEL_SCOPE.y + 1, PANEL_SCOPE.w - 2,
                 PANEL_SCOPE.h - 2};
    int nch = std::min(playback::trackerChannels(), 32);
    float buf[2048];

    if (nch <= 0) {  // pocketmod fallback: single mixed scope
        int n = playback::visSamples(buf, 1600);
        drawWave(r, in, buf, n);
        drawTextRaw(r, "MIX", in.x + 4, in.y + 3, 1, {60, 130, 80});
        return;
    }

    int cols = nch <= 4 ? nch : nch <= 8 ? 4 : 8;
    int rows = (nch + cols - 1) / cols;
    float cw = in.w / cols, chh = in.h / rows;
    int want = std::min(2048, playback::trackerScopeRate() * 35 / 1000);
    for (int ch = 0; ch < nch; ++ch) {
        SDL_FRect cell{in.x + (ch % cols) * cw, in.y + (ch / cols) * chh,
                       cw, chh};
        int n = playback::trackerScope(ch, buf, want);
        drawWave(r, {cell.x + 1, cell.y + 1, cell.w - 2, cell.h - 2}, buf, n);
        char lab[16];
        std::snprintf(lab, sizeof lab, "%d", ch + 1);
        drawTextRaw(r, lab, cell.x + 3, cell.y + 2, 1, {60, 130, 80});
        setCol(r, GRID);  // cell separators
        if (ch % cols) fillR(r, cell.x, cell.y, 1, cell.h);
        if (ch / cols) fillR(r, cell.x, cell.y, cell.w, 1);
    }
}

// ------------------------------------------------------------ pattern view

// Cell text is playback's fixed 14-char "C-5 04v0F A06 " layout: note,
// instrument, volume, effect. Color the fields FT2-style, empty ones dimmed.
void drawCell(SDL_Renderer* r, const std::string& s, float x, float y,
              int chars) {
    struct { int off, len; Col c; } f[] = {
        {0, 3, F_NOTE}, {4, 2, F_INST}, {6, 3, F_VOL}, {10, 3, F_FX},
    };
    for (const auto& fld : f) {
        if (fld.off >= chars || fld.off >= (int)s.size()) break;
        int len = std::min(fld.len, chars - fld.off);
        std::string part = s.substr(fld.off, len);
        bool empty = part.find_first_not_of(". ") == std::string::npos;
        drawTextRaw(r, part, x + fld.off * ADV, y, 1, empty ? F_EMPTY : fld.c);
    }
}

void drawPattern(SDL_Renderer* r) {
    bevel(r, PANEL_PAT, PAT_BG, true);
    SDL_FRect in{PANEL_PAT.x + 1, PANEL_PAT.y + 1, PANEL_PAT.w - 2,
                 PANEL_PAT.h - 2};
    float cx = in.x + in.w / 2;

    playback::TrackerState st{};
    if (!playback::trackerState(st)) {
        drawTextCentered(r, playback::active()
                                ? "pattern data needs libopenmpt"
                                : "no module loaded",
                         cx, in.y + in.h / 2 - 4, 1, F_EMPTY);
        return;
    }

    const float rowH = LIST_ROW, gut = 22;
    const float patX = in.x + gut, patW = in.w - 2 * gut;
    int nch = playback::trackerChannels();
    // Adaptive channel width: shrink each column's text before dropping
    // channels; never below the 3-char note.
    int nshow = std::min(nch, (int)(patW / (3 * ADV + 4)));
    if (nshow <= 0) return;
    float chW = patW / nshow;
    // snap the visible cell text to a field boundary: note / +instrument /
    // +volume / +effect
    int fit = (int)((chW - 4) / ADV), chars = 3;
    for (int b : {13, 9, 6})
        if (fit >= b) { chars = b; break; }

    // channel header strip
    for (int c = 0; c < nshow; ++c) {
        char lab[16];
        std::snprintf(lab, sizeof lab, "%02d", c + 1);
        drawTextCentered(r, lab, patX + c * chW + chW / 2, in.y + 3, 1, DIM);
    }
    if (nshow < nch) {  // channels that don't fit
        char more[16];
        std::snprintf(more, sizeof more, "+%d", nch - nshow);
        drawTextRaw(r, more, in.x + in.w - 4 - textW(more, 1), in.y + 3, 1,
                    DIM);
    }
    setCol(r, GRID);
    fillR(r, in.x, in.y + 12, in.w, 1);

    const float top = in.y + 14;
    const int nrows = (int)((in.h - 16) / rowH), center = nrows / 2;
    float barY = top + center * rowH;

    // beat tint + center bar behind the text
    for (int i = 0; i < nrows; ++i) {
        int row = st.row + i - center;
        if (row < 0 || row >= st.numRows || row % 4) continue;
        setCol(r, PAT_BEAT);
        fillR(r, in.x, top + i * rowH - 1, in.w, rowH);
    }
    setCol(r, PAT_BAR);
    fillR(r, in.x, barY - 1, in.w, rowH);

    for (int i = 0; i < nrows; ++i) {
        int row = st.row + i - center;
        if (row < 0 || row >= st.numRows) continue;
        float y = top + i * rowH;
        char num[16];
        std::snprintf(num, sizeof num, "%02d", row);
        Col rn = i == center ? C_WHITE : ROWNUM;
        drawTextRaw(r, num, in.x + 4, y, 1, rn);
        drawTextRaw(r, num, in.x + in.w - 4 - textW(num, 1), y, 1, rn);
        for (int c = 0; c < nshow; ++c)
            drawCell(r, playback::trackerCell(st.pattern, row, c),
                     patX + c * chW + 2, y, chars);
        if (nshow > 1) {  // faint channel separators
            setCol(r, GRID);
            for (int c = 1; c < nshow; ++c)
                fillR(r, patX + c * chW - 1, y, 1, rowH);
        }
    }
}

// ------------------------------------------------------------------ render

void render(SDL_Renderer* r) {
    setCol(r, DARK);
    fillR(r, 0, 0, W, H);
    drawInfo(r);
    drawCtrl(r);
    drawList(r);
    drawScopes(r);
    drawPattern(r);
}

// ------------------------------------------------------------------- input

int s_drag = 0;  // 1 = seek, 2 = volume

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
                    else if (i == 3) playback::stop();
                    else nextTrack();
                    return;
                }
            if (g.nav.size() > 1 && inRect(x, y, RC_BACK)) { goBack(); break; }
            if (inRect(x, y, PANEL_LIST) && y >= PANEL_LIST.y + LIST_Y &&
                !showingInstruments() && g.nav.back().view == View::LIST) {
                Page& p = g.nav.back();
                int idx = p.scroll +
                          (int)((y - PANEL_LIST.y - LIST_Y) / LIST_ROW);
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

const Skin kSkinTracker = {"tracker", W, H, 2, render, event};
