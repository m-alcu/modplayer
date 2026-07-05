// Skin: foobar2000 — the deliberately plain Win32 look: menu bar, toolbar
// with transport + seekbar, a columned playlist (Title | Artist | Album) and
// a status bar. Single click selects, double click activates (rows with a
// ">" submenu still open on single click, like folders in a file manager
// would not — so double click those too). Now Playing renders the play
// queue with the current row marked, the foobar way.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr int W = 400, H = 300;

constexpr Col CHROME {212, 208, 200};     // classic Win32 gray
constexpr Col CHROME_D{128, 128, 128};
constexpr Col CHROME_L{255, 255, 255};
constexpr Col LIST_BG{255, 255, 255};
constexpr Col LIST_ALT{244, 246, 249};
constexpr Col SEL_BG {10, 36, 106};
constexpr Col INK    {0, 0, 0};
constexpr Col DIMINK {96, 96, 96};

constexpr float MENU_H = 14, TOOL_H = 24, HDRROW = 14, STATUS_H = 16;
constexpr float LIST_Y = MENU_H + TOOL_H + HDRROW;
constexpr float LIST_H = H - LIST_Y - STATUS_H;
constexpr float ROW_H = 13;
// columns: Title | Artist | Album
constexpr float C1 = 6, C2 = 170, C3 = 288;

constexpr SDL_FRect RC_SEEK{150, MENU_H + 8, 180, 9};
constexpr SDL_FRect RC_VOL {340, MENU_H + 8, 54, 9};
constexpr float BTN_X = 26, BTN_W = 20, BTN_H = 16;  // stop play pause prev next

void frame(SDL_Renderer* r, SDL_FRect rc, bool sunken) {
    setCol(r, sunken ? CHROME_D : CHROME_L);
    fillR(r, rc.x, rc.y, rc.w, 1);
    fillR(r, rc.x, rc.y, 1, rc.h);
    setCol(r, sunken ? CHROME_L : CHROME_D);
    fillR(r, rc.x, rc.y + rc.h - 1, rc.w, 1);
    fillR(r, rc.x + rc.w - 1, rc.y, 1, rc.h);
}

void drawChrome(SDL_Renderer* r, Page& p) {
    setCol(r, CHROME);
    fillR(r, 0, 0, W, LIST_Y);
    fillR(r, 0, H - STATUS_H, W, STATUS_H);

    // menu bar (decorative except Back)
    float x = 6;
    const char* menus[] = {"File", "Edit", "View", "Playback", "Library",
                           "Help"};
    for (const char* m : menus) {
        drawTextRaw(r, m, x, 3, 1, INK);
        x += textW(m, 1) + 12;
    }

    // toolbar: back + transport + seek + volume
    float by = MENU_H + 4;
    SDL_FRect back{4, by, BTN_W, BTN_H};
    frame(r, back, false);
    drawTextRaw(r, "<", back.x + 7, back.y + 4, 1, INK,
                g.nav.size() > 1);
    const char* glyphs[] = {"#", ">", "\"", "«", "»"};  // stop play pause
    for (int i = 0; i < 5; ++i) {
        SDL_FRect b{BTN_X + 2 + i * (BTN_W + 2), by, BTN_W, BTN_H};
        frame(r, b, false);
        float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        switch (i) {
            case 0: setCol(r, INK); fillR(r, cx - 4, cy - 4, 8, 8); break;
            case 1: fillTriangle(r, cx - 3, cy - 5, cx - 3, cy + 5, cx + 5,
                                 cy, INK); break;
            case 2: setCol(r, INK); fillR(r, cx - 4, cy - 4, 3, 8);
                    fillR(r, cx + 1, cy - 4, 3, 8); break;
            case 3: fillTriangle(r, cx + 3, cy - 4, cx + 3, cy + 4, cx - 4,
                                 cy, INK);
                    setCol(r, INK); fillR(r, cx - 5, cy - 4, 2, 8); break;
            case 4: fillTriangle(r, cx - 3, cy - 4, cx - 3, cy + 4, cx + 4,
                                 cy, INK);
                    setCol(r, INK); fillR(r, cx + 3, cy - 4, 2, 8); break;
        }
        (void)glyphs;
    }

    // seekbar
    frame(r, RC_SEEK, true);
    setCol(r, CHROME_L);
    fillR(r, RC_SEEK.x + 1, RC_SEEK.y + 1, RC_SEEK.w - 2, RC_SEEK.h - 2);
    double dur = playback::duration();
    float frac = dur > 0 ? (float)(playback::position() / dur) : 0;
    setCol(r, SEL_BG);
    fillR(r, RC_SEEK.x + 1, RC_SEEK.y + 1, (RC_SEEK.w - 2) * frac,
          RC_SEEK.h - 2);

    // volume
    frame(r, RC_VOL, true);
    setCol(r, CHROME_L);
    fillR(r, RC_VOL.x + 1, RC_VOL.y + 1, RC_VOL.w - 2, RC_VOL.h - 2);
    setCol(r, {60, 120, 60});
    fillR(r, RC_VOL.x + 1, RC_VOL.y + 1, (RC_VOL.w - 2) * playback::volume(),
          RC_VOL.h - 2);

    // column header row
    float hy = MENU_H + TOOL_H;
    setCol(r, CHROME);
    fillR(r, 0, hy, W, HDRROW);
    frame(r, {0, hy, C2 - 2, HDRROW}, false);
    frame(r, {C2 - 2, hy, C3 - C2, HDRROW}, false);
    frame(r, {C3 - 2, hy, W - C3 + 2, HDRROW}, false);
    const bool tracks = !p.rows.empty() && p.rows[0].track >= 0;
    drawTextRaw(r, tracks ? "Title" : foldAscii(p.title).c_str(), C1 + 2,
                hy + 3, 1, INK);
    if (tracks) {
        drawTextRaw(r, "Artist", C2 + 2, hy + 3, 1, INK);
        drawTextRaw(r, "Album", C3 + 2, hy + 3, 1, INK);
    }

    // status bar
    frame(r, {0, H - STATUS_H, W, STATUS_H}, true);
    std::string st;
    const Track* t = currentTrack();
    if (t && playback::active()) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s | %d Hz | %s | %s / %s",
                      playback::paused() ? "Paused" : "Playing",
                      playback::sampleRate(),
                      playback::channels() == 1 ? "mono" : "stereo",
                      fmtTime(playback::position()).c_str(),
                      fmtTime(playback::duration()).c_str());
        st = buf;
    } else {
        char buf[64];
        std::snprintf(buf, sizeof buf, "Stopped | %d tracks",
                      (int)g.lib.tracks().size());
        st = buf;
    }
    drawText(r, st, 5, H - STATUS_H + 4, 1, INK, W - 10);
}

// Rows for the list area: navigation pages show their rows; Now Playing
// shows the queue.
struct RowView {
    std::string a, b, c;   // columns
    bool playing = false;
};

RowView rowView(const Row& row) {
    RowView v;
    v.a = row.label;
    if (row.track >= 0) {
        const Track& t = g.lib.tracks()[row.track];
        v.a = t.title;
        v.b = t.meta.artist.empty() ? Library::kUnknownArtist : t.meta.artist;
        v.c = t.meta.album.empty() ? Library::kUnknownAlbum : t.meta.album;
    } else if (row.arrow) {
        v.a += " >";
    }
    return v;
}

void drawRows(SDL_Renderer* r, Page& p) {
    setCol(r, LIST_BG);
    fillR(r, 0, LIST_Y, W, LIST_H);

    const bool queueMode = p.view == View::NOW_PLAYING;
    const int n = queueMode ? (int)g.queue.size() : (int)p.rows.size();
    const int visible = (int)(LIST_H / ROW_H);
    int cursor = queueMode ? g.qpos : p.cursor;
    int& scroll = p.scroll;
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + visible) scroll = cursor - visible + 1;
    scroll = std::clamp(scroll, 0, std::max(0, n - visible));

    for (int i = 0; i < visible; ++i) {
        int idx = scroll + i;
        if (idx >= n) break;
        float y = LIST_Y + i * ROW_H;
        RowView v;
        bool sel;
        if (queueMode) {
            Row fake;
            fake.track = g.queue[idx];
            v = rowView(fake);
            sel = idx == g.qpos;
        } else {
            v = rowView(p.rows[idx]);
            sel = idx == p.cursor;
        }
        if (idx % 2 && !sel) {
            setCol(r, LIST_ALT);
            fillR(r, 0, y, W, ROW_H);
        }
        if (sel) {
            setCol(r, SEL_BG);
            fillR(r, 0, y, W, ROW_H);
        }
        Col tc = sel ? Col{255, 255, 255} : INK;
        drawText(r, v.a, C1, y + 3, 1, tc, C2 - C1 - 8);
        if (!v.b.empty()) drawText(r, v.b, C2, y + 3, 1, tc, C3 - C2 - 8);
        if (!v.c.empty()) drawText(r, v.c, C3, y + 3, 1, tc, W - C3 - 8);
    }

    if (n == 0)
        drawText(r, queueMode ? "Queue is empty" : "No songs found", C1,
                 LIST_Y + 20, 1, DIMINK);

    if (n > visible) {  // classic scrollbar groove
        setCol(r, {232, 232, 232});
        fillR(r, W - 7, LIST_Y, 7, LIST_H);
        float barH = std::max(16.0f, LIST_H * visible / n);
        float t = (float)scroll / (n - visible);
        SDL_FRect thumb{W - 7, LIST_Y + t * (LIST_H - barH), 7, barH};
        setCol(r, CHROME);
        fillR(r, thumb.x, thumb.y, thumb.w, thumb.h);
        frame(r, thumb, false);
    }
}

void render(SDL_Renderer* r) {
    Page& p = g.nav.back();
    drawChrome(r, p);
    drawRows(r, p);
}

void event(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return;
    float x = e.button.x, y = e.button.y;
    Page& p = g.nav.back();

    if (y >= MENU_H && y < MENU_H + TOOL_H) {   // toolbar
        if (x >= 4 && x < 4 + BTN_W) { goBack(); return; }
        for (int i = 0; i < 5; ++i) {
            SDL_FRect b{BTN_X + 2 + i * (BTN_W + 2), MENU_H + 4, BTN_W,
                        BTN_H};
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                switch (i) {
                    case 0: playback::stop(); break;
                    case 1: playOrResume(); break;
                    case 2: playPauseToggle(); break;
                    case 3: prevTrack(); break;
                    case 4: nextTrack(); break;
                }
                return;
            }
        }
        if (x >= RC_SEEK.x && x < RC_SEEK.x + RC_SEEK.w) {
            float f = (x - RC_SEEK.x) / RC_SEEK.w;
            playback::seekTo(f * playback::duration());
            return;
        }
        if (x >= RC_VOL.x && x < RC_VOL.x + RC_VOL.w) {
            playback::setVolume((x - RC_VOL.x) / RC_VOL.w);
            return;
        }
        return;
    }

    if (y < LIST_Y || y >= LIST_Y + LIST_H) return;
    if (p.view == View::NOW_PLAYING) return;    // queue view is read-only

    int idx = p.scroll + (int)((y - LIST_Y) / ROW_H);
    if (idx < 0 || idx >= (int)p.rows.size()) return;

    // double click activates; single click selects
    static Uint64 lastClick = 0;
    static int lastIdx = -1;
    Uint64 now = SDL_GetTicks();
    bool dbl = idx == lastIdx && now - lastClick < 400;
    lastClick = now;
    lastIdx = idx;
    p.cursor = idx;
    if (dbl) activateRow(p, idx);
}

}  // namespace

const Skin kSkinFoobar = {"foobar", W, H, 2, render, event};
