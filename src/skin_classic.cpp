// Skin: Apple iPod Classic (6G) — anodized-gray portrait body with the
// landscape split screen: menu list on the left half, a preview pane (cover
// art or library stats) on the right, and the big chrome-ring click wheel.
// Wheel interaction matches the nano skin.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core.h"
#include "playback.h"

namespace {

constexpr float WIN_W = 400, WIN_H = 640;
constexpr SDL_FRect SCR{40, 36, 320, 240};             // landscape screen
constexpr float HDR_H = 20;
constexpr float SPLIT = SCR.x + SCR.w * 0.52f;         // list | preview divide
constexpr float WHEEL_CX = 200, WHEEL_CY = 480;
constexpr float WHEEL_R = 118, HUB_R = 46;

void drawHeader(SDL_Renderer* r, const std::string& title) {
    vGradient(r, {SCR.x, SCR.y, SCR.w, HDR_H}, {58, 62, 72}, {24, 26, 32});
    setCol(r, {90, 95, 106});
    fillR(r, SCR.x, SCR.y + HDR_H - 1, SCR.w, 1);
    drawTextCentered(r, title, SCR.x + SCR.w / 2, SCR.y + 6, 1, C_WHITE, true);
    // battery, right
    setCol(r, {150, 155, 164});
    fillR(r, SCR.x + SCR.w - 28, SCR.y + 5, 18, 9);
    fillR(r, SCR.x + SCR.w - 10, SCR.y + 7.5f, 2, 4);
    SDL_SetRenderDrawColor(r, 92, 190, 90, 255);
    fillR(r, SCR.x + SCR.w - 27, SCR.y + 6, 16, 7);
    // play-state icon, left
    if (playback::active()) {
        float ix = SCR.x + 8, iy = SCR.y + 5;
        if (playback::paused()) {
            setCol(r, {150, 155, 164});
            fillR(r, ix, iy, 3, 9);
            fillR(r, ix + 5, iy, 3, 9);
        } else {
            fillTriangle(r, ix, iy, ix, iy + 9, ix + 8, iy + 4.5f,
                         {150, 155, 164});
        }
    }
}

void drawArtBox(SDL_Renderer* r, SDL_FRect box) {
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
}

// Right pane of the split view: cover of the playing track, else library
// stats — the Classic showed a montage there; stats keep it informative.
void drawPreview(SDL_Renderer* r) {
    SDL_FRect pane{SPLIT, SCR.y + HDR_H, SCR.x + SCR.w - SPLIT,
                   SCR.h - HDR_H};
    vGradient(r, pane, {32, 34, 40}, {16, 17, 21});
    setCol(r, {90, 95, 106});
    fillR(r, pane.x, pane.y, 1, pane.h);

    const Track* t = currentTrack();
    if (t) {
        float side = std::min(pane.w - 28, pane.h - 74);
        SDL_FRect box{pane.x + (pane.w - side) / 2, pane.y + 12, side, side};
        drawArtBox(r, box);
        float ty = box.y + box.h + 8;
        drawText(r, t->title, pane.x + 10, ty, 1, C_WHITE, pane.w - 20, true);
        std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                    : t->meta.artist;
        drawText(r, artist, pane.x + 10, ty + 16, 1, {170, 175, 186},
                 pane.w - 20);
        double dur = playback::duration();
        if (dur > 0) {
            std::string tm = fmtTime(playback::position()) + " / " +
                             fmtTime(dur);
            drawText(r, tm, pane.x + 10, ty + 32, 1, {170, 175, 186});
        }
    } else {
        int nsongs = (int)g.lib.tracks().size();
        int nart = (int)g.lib.artists().size();
        int nalb = (int)g.lib.albums().size();
        float cx = pane.x + pane.w / 2, y = pane.y + 40;
        drawNoArt(r, {cx - 40, y, 80, 80}, {46, 49, 58}, {28, 30, 36},
                  {110, 116, 128});
        y += 96;
        char buf[48];
        std::snprintf(buf, sizeof buf, "%d songs", nsongs);
        drawTextCentered(r, buf, cx, y, 1, {170, 175, 186});
        std::snprintf(buf, sizeof buf, "%d artists", nart);
        drawTextCentered(r, buf, cx, y + 16, 1, {170, 175, 186});
        std::snprintf(buf, sizeof buf, "%d albums", nalb);
        drawTextCentered(r, buf, cx, y + 32, 1, {170, 175, 186});
    }
}

void drawList(SDL_Renderer* r, Page& p) {
    const float rowH = 20;
    const float listY = SCR.y + HDR_H;
    const float listW = SPLIT - SCR.x;
    const int visible = (int)((SCR.h - HDR_H) / rowH);

    if (p.cursor < p.scroll) p.scroll = p.cursor;
    if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;

    for (int i = 0; i < visible; ++i) {
        int idx = p.scroll + i;
        if (idx >= (int)p.rows.size()) break;
        float y = listY + i * rowH;
        bool sel = idx == p.cursor;
        if (sel)
            vGradient(r, {SCR.x, y, listW, rowH}, C_SEL_TOP, C_SEL_BOT);
        Col tc = sel ? C_WHITE : C_TEXT;
        drawText(r, p.rows[idx].label, SCR.x + 7, y + 6, 1, tc, listW - 22,
                 sel);
        if (p.rows[idx].arrow)
            drawTextRaw(r, ">", SCR.x + listW - 11, y + 6, 1, tc, sel);
    }

    if (p.rows.empty()) {
        drawTextCentered(r, "No songs found", SCR.x + listW / 2, SCR.y + 100,
                         1, C_GRAY);
    }

    if ((int)p.rows.size() > visible) {
        float trackH = SCR.h - HDR_H - 4;
        float barH = std::max(12.0f, trackH * visible / p.rows.size());
        float t = p.rows.size() - visible > 0
                      ? (float)p.scroll / (p.rows.size() - visible) : 0;
        setCol(r, {225, 227, 231});
        fillR(r, SCR.x + listW - 5, listY + 2, 3, trackH);
        setCol(r, {150, 155, 164});
        fillR(r, SCR.x + listW - 5, listY + 2 + t * (trackH - barH), 3, barH);
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

    SDL_FRect box{SCR.x + 16, SCR.y + HDR_H + 26, 120, 120};
    drawArtBox(r, box);

    float tx = box.x + box.w + 14;
    float tmax = SCR.x + SCR.w - 10 - tx;
    drawText(r, t->title, tx, box.y + 10, 1, C_TEXT, tmax, true);
    std::string artist = t->meta.artist.empty() ? Library::kUnknownArtist
                                                : t->meta.artist;
    std::string album = t->meta.album.empty() ? Library::kUnknownAlbum
                                              : t->meta.album;
    drawText(r, artist, tx, box.y + 32, 1, C_GRAY, tmax);
    drawText(r, album, tx, box.y + 52, 1, C_GRAY, tmax);
    if (t->meta.year)
        drawText(r, std::to_string(t->meta.year), tx, box.y + 72, 1, C_GRAY);

    // progress / volume bar
    float bx = SCR.x + 16, bw = SCR.w - 32, by = SCR.y + SCR.h - 38;
    bool volMode = SDL_GetTicks() < g.volOverlayUntil;
    setCol(r, {210, 213, 219});
    fillR(r, bx, by, bw, 8);
    if (volMode) {
        vGradient(r, {bx, by, bw * playback::volume(), 8}, C_SEL_TOP,
                  C_SEL_BOT);
        drawTextCentered(r, "Volume", SCR.x + SCR.w / 2, by + 14, 1, C_GRAY);
    } else {
        double dur = playback::duration(), pos = playback::position();
        float frac = dur > 0 ? (float)(pos / dur) : 0;
        vGradient(r, {bx, by, bw * frac, 8}, C_SEL_TOP, C_SEL_BOT);
        float kx = bx + bw * frac;
        fillTriangle(r, kx - 5, by + 4, kx, by - 2, kx + 5, by + 4, C_TEXT);
        fillTriangle(r, kx - 5, by + 4, kx, by + 10, kx + 5, by + 4, C_TEXT);
        drawText(r, fmtTime(pos), bx, by + 14, 1, C_GRAY);
        std::string rem = "-" + fmtTime(dur - pos);
        drawText(r, rem, bx + bw - textW(rem, 1), by + 14, 1, C_GRAY);
    }
    setCol(r, {160, 164, 172});
    SDL_FRect orc{bx, by, bw, 8};
    SDL_RenderRect(r, &orc);
}

void drawWheel(SDL_Renderer* r) {
    // chrome ring + dark wheel face, Classic style
    fillCircle(r, WHEEL_CX, WHEEL_CY + 2, WHEEL_R + 5, {60, 62, 68});
    fillCircle(r, WHEEL_CX, WHEEL_CY, WHEEL_R + 4, {212, 215, 221});
    fillCircle(r, WHEEL_CX, WHEEL_CY, WHEEL_R, {96, 100, 110});
    fillRing(r, WHEEL_CX, WHEEL_CY, HUB_R + 2, HUB_R, {70, 73, 82});
    fillCircle(r, WHEEL_CX, WHEEL_CY, HUB_R, {182, 186, 194});

    Col lc{210, 213, 220};
    drawTextCentered(r, "MENU", WHEEL_CX, WHEEL_CY - WHEEL_R + 20, 1, lc,
                     true);

    float py = WHEEL_CY + WHEEL_R - 26;
    fillTriangle(r, WHEEL_CX - 10, py, WHEEL_CX - 10, py + 10, WHEEL_CX - 2,
                 py + 5, lc);
    setCol(r, lc);
    fillR(r, WHEEL_CX + 2, py, 3, 10);
    fillR(r, WHEEL_CX + 7, py, 3, 10);

    float lx = WHEEL_CX - WHEEL_R + 18, ly = WHEEL_CY;
    setCol(r, lc);
    fillR(r, lx, ly - 5, 3, 10);
    fillTriangle(r, lx + 12, ly - 5, lx + 12, ly + 5, lx + 4, ly, lc);
    fillTriangle(r, lx + 20, ly - 5, lx + 20, ly + 5, lx + 12, ly, lc);

    float rx = WHEEL_CX + WHEEL_R - 18, ry = WHEEL_CY;
    setCol(r, lc);
    fillR(r, rx - 3, ry - 5, 3, 10);
    fillTriangle(r, rx - 24, ry - 5, rx - 24, ry + 5, rx - 16, ry, lc);
    fillTriangle(r, rx - 16, ry - 5, rx - 16, ry + 5, rx - 8, ry, lc);
}

void render(SDL_Renderer* ren) {
    SDL_SetRenderDrawColor(ren, 50, 52, 58, 255);
    SDL_RenderClear(ren);

    // anodized aluminum front, darker steel back edge
    fillRoundRect(ren, {10, 8, WIN_W - 20, WIN_H - 16}, 28, {70, 72, 80});
    fillRoundRect(ren, {12, 10, WIN_W - 24, WIN_H - 20}, 26, {132, 136, 144});
    vGradient(ren, {14, 34, 8, WIN_H - 68}, {170, 174, 182}, {104, 108, 118});

    // screen bezel + screen
    setCol(ren, {28, 30, 36});
    fillR(ren, SCR.x - 6, SCR.y - 6, SCR.w + 12, SCR.h + 12);
    setCol(ren, C_WHITE);
    fillR(ren, SCR.x, SCR.y, SCR.w, SCR.h);

    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) {
        drawNowPlaying(ren);
    } else {
        drawHeader(ren, g.nav.size() == 1 ? "iPod" : p.title);
        drawList(ren, p);
        drawPreview(ren);
    }

    drawWheel(ren);
}

void wheelClick(float dx, float dy, float dist) {
    if (dist <= HUB_R) { selectPress(); return; }
    float ang = std::atan2(dy, dx);
    const float q = (float)M_PI / 4;
    if (ang > -3 * q && ang <= -q) goBack();              // MENU (top)
    else if (ang > -q && ang <= q) nextTrack();           // >>|
    else if (ang > q && ang <= 3 * q) playPauseToggle();  // play/pause
    else prevTrack();                                     // |<<
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
            if (std::sqrt(dx * dx + dy * dy) < 12) break;
            float ang = std::atan2(dy, dx);
            float d = ang - g.wheelAngle;
            while (d > (float)M_PI) d -= 2 * (float)M_PI;
            while (d < -(float)M_PI) d += 2 * (float)M_PI;
            g.wheelAngle = ang;
            g.wheelAccum += d;
            const float step = 0.30f;
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

const Skin kSkinClassic = {"classic", (int)WIN_W, (int)WIN_H, 1, render, event};
