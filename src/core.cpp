#include "core.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>

#include "font8x8.h"
#include "playback.h"
#include "tags.h"

App g;

// -------------------------------------------------------------- primitives

void setCol(SDL_Renderer* r, Col c, Uint8 a) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
}

void fillR(SDL_Renderer* r, float x, float y, float w, float h) {
    SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

void fillCircle(SDL_Renderer* r, float cx, float cy, float rad, Col c,
                Uint8 a) {
    setCol(r, c, a);
    for (int dy = (int)-rad; dy <= (int)rad; ++dy) {
        float w = std::sqrt(std::max(0.0f, rad * rad - dy * dy));
        fillR(r, cx - w, cy + dy, 2 * w, 1);
    }
}

void fillRing(SDL_Renderer* r, float cx, float cy, float rOut, float rIn,
              Col c) {
    setCol(r, c);
    for (int dy = (int)-rOut; dy <= (int)rOut; ++dy) {
        float wo = std::sqrt(std::max(0.0f, rOut * rOut - dy * dy));
        float wi = std::abs((float)dy) < rIn
                       ? std::sqrt(std::max(0.0f, rIn * rIn - dy * dy))
                       : 0;
        if (wi > 0) {
            fillR(r, cx - wo, cy + dy, wo - wi, 1);
            fillR(r, cx + wi, cy + dy, wo - wi, 1);
        } else {
            fillR(r, cx - wo, cy + dy, 2 * wo, 1);
        }
    }
}

void fillRoundRect(SDL_Renderer* r, SDL_FRect rc, float rad, Col c) {
    setCol(r, c);
    for (float y = 0; y < rc.h; ++y) {
        float inset = 0;
        if (y < rad) {
            float d = rad - y;
            inset = rad - std::sqrt(std::max(0.0f, rad * rad - d * d));
        } else if (y > rc.h - rad) {
            float d = y - (rc.h - rad);
            inset = rad - std::sqrt(std::max(0.0f, rad * rad - d * d));
        }
        fillR(r, rc.x + inset, rc.y + y, rc.w - 2 * inset, 1);
    }
}

void vGradient(SDL_Renderer* r, SDL_FRect rc, Col top, Col bot) {
    for (float y = 0; y < rc.h; ++y) {
        float t = y / rc.h;
        SDL_SetRenderDrawColor(r, (Uint8)(top.r + t * (bot.r - top.r)),
                               (Uint8)(top.g + t * (bot.g - top.g)),
                               (Uint8)(top.b + t * (bot.b - top.b)), 255);
        fillR(r, rc.x, rc.y + y, rc.w, 1);
    }
}

void fillTriangle(SDL_Renderer* r, float x0, float y0, float x1, float y1,
                  float x2, float y2, Col c) {
    SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f};
    SDL_Vertex v[3] = {{{x0, y0}, fc, {0, 0}},
                       {{x1, y1}, fc, {0, 0}},
                       {{x2, y2}, fc, {0, 0}}};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
}

// -------------------------------------------------------------------- text

// Fold UTF-8 to the ASCII the 8x8 font can draw: strip the common Latin
// accents (á→a, ñ→n, …), replace anything else with '?'.
std::string foldAscii(const std::string& u) {
    static const char* kLatin1 =  // U+00C0..U+00FF
        "AAAAAA?CEEEEIIIIDNOOOOOx0UUUUY??aaaaaa?ceeeeiiiidnooooo/0uuuuy?y";
    std::string out;
    for (size_t i = 0; i < u.size();) {
        unsigned char c = u[i];
        if (c < 0x80) { out += (char)c; ++i; continue; }
        uint32_t cp = 0; int len = 1;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        for (int k = 1; k < len && i + k < u.size(); ++k)
            cp = (cp << 6) | (u[i + k] & 0x3F);
        i += len;
        if (cp >= 0xC0 && cp <= 0xFF) out += kLatin1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) out += "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs"[cp - 0x100];
        else out += '?';
    }
    return out;
}

int textW(const std::string& s, int scale) {
    return (int)s.size() * ADV * scale;
}

void drawTextRaw(SDL_Renderer* r, const std::string& ascii, float x, float y,
                 int scale, Col c, bool bold) {
    setCol(r, c);
    for (char chs : ascii) {
        unsigned char ch = (unsigned char)chs;
        if (ch >= 128) ch = '?';
        for (int row = 0; row < 8; ++row) {
            unsigned bits = font8x8_basic[ch][row];
            for (int col = 0; col < 8; ++col)
                if (bits & (1u << col)) {
                    fillR(r, x + col * scale, y + row * scale, (float)scale,
                          (float)scale);
                    if (bold)
                        fillR(r, x + col * scale + 1, y + row * scale,
                              (float)scale, (float)scale);
                }
        }
        x += ADV * scale;
    }
}

void drawText(SDL_Renderer* r, const std::string& utf8, float x, float y,
              int scale, Col c, float maxW, bool bold) {
    std::string s = foldAscii(utf8);
    if (maxW > 0 && textW(s, scale) > maxW) {
        int fit = (int)(maxW / (ADV * scale));
        if (fit < 3) fit = 3;
        s = s.substr(0, (size_t)fit - 2) + "..";
    }
    drawTextRaw(r, s, x, y, scale, c, bold);
}

void drawTextCentered(SDL_Renderer* r, const std::string& utf8, float cx,
                      float y, int scale, Col c, bool bold) {
    std::string s = foldAscii(utf8);
    drawTextRaw(r, s, cx - textW(s, scale) / 2.0f, y, scale, c, bold);
}

std::string fmtTime(double s) {
    if (s < 0) s = 0;
    int m = (int)s / 60, sec = (int)s % 60;
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", m, sec);
    return buf;
}

void drawNoArt(SDL_Renderer* r, SDL_FRect rc, Col top, Col bot, Col note) {
    vGradient(r, rc, top, bot);
    float cx = rc.x + rc.w * 0.42f, cy = rc.y + rc.h * 0.66f;
    fillCircle(r, cx, cy, rc.w * 0.10f, note);
    fillCircle(r, cx + rc.w * 0.24f, cy - rc.h * 0.06f, rc.w * 0.10f, note);
    setCol(r, note);
    fillR(r, cx + rc.w * 0.10f - 2, cy - rc.h * 0.36f, 3, rc.h * 0.36f);
    fillR(r, cx + rc.w * 0.34f - 2, cy - rc.h * 0.42f, 3, rc.h * 0.36f);
    fillR(r, cx + rc.w * 0.10f - 2, cy - rc.h * 0.36f, rc.w * 0.24f + 3, 4);
}

// --------------------------------------------------------------- app state

const Track* currentTrack() {
    if (g.qpos < 0 || g.qpos >= (int)g.queue.size()) return nullptr;
    return &g.lib.tracks()[g.queue[g.qpos]];
}

// Submenu action ids (private to the page system).
enum {
    A_NONE, A_MUSIC, A_ARTISTS, A_ALBUMS, A_SONGS, A_YEARS, A_SHUFFLE,
    A_NOW_PLAYING, A_ARTIST, A_ARTIST_ALL, A_ALBUM, A_ALBUM_OF_ARTIST, A_YEAR,
};

Page pageMain() {
    Page p;
    p.title = "Menu";  // the nano skin renames the root page to "iPod"
    p.rows = {{"Music", true, -1, A_MUSIC, "", -1},
              {"Shuffle Songs", false, -1, A_SHUFFLE, "", -1},
              {"Now Playing", true, -1, A_NOW_PLAYING, "", -1}};
    return p;
}

Page pageMusic() {
    Page p;
    p.title = "Music";
    p.rows = {{"Artists", true, -1, A_ARTISTS, "", -1},
              {"Albums", true, -1, A_ALBUMS, "", -1},
              {"Songs", true, -1, A_SONGS, "", -1},
              {"Years", true, -1, A_YEARS, "", -1}};
    return p;
}

Page pageSongs(const std::string& title, const std::vector<int>& idxs) {
    Page p;
    p.title = title;
    for (int i : idxs)
        p.rows.push_back({g.lib.tracks()[i].title, false, i, A_NONE, "", -1});
    return p;
}

static Page pageArtists() {
    Page p;
    p.title = "Artists";
    for (const std::string& a : g.lib.artists())
        p.rows.push_back({a, true, -1, A_ARTIST, a, -1});
    return p;
}

static Page pageArtistAlbums(const std::string& artist) {
    Page p;
    p.title = artist;
    p.rows.push_back({"All", true, -1, A_ARTIST_ALL, artist, -1});
    for (const std::string& al : g.lib.albumsOf(artist))
        p.rows.push_back({al, true, -1, A_ALBUM_OF_ARTIST, artist + "\n" + al, -1});
    return p;
}

static Page pageAlbums() {
    Page p;
    p.title = "Albums";
    for (const std::string& al : g.lib.albums())
        p.rows.push_back({al, true, -1, A_ALBUM, al, -1});
    return p;
}

static Page pageYears() {
    Page p;
    p.title = "Years";
    for (int y : g.lib.years()) {
        std::string label = y ? std::to_string(y) : "Unknown";
        p.rows.push_back({label, true, -1, A_YEAR, "", y});
    }
    return p;
}

// ------------------------------------------------------------- playback glue

// Folder art fallback for tracks with no embedded picture: look for an image
// next to the file — cover/folder/front/album/art.(png|jpg|jpeg|bmp), any
// case — or a lone image file of any name. Cached per directory.
static std::string folderArtFor(const std::string& trackPath) {
    size_t slash = trackPath.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : trackPath.substr(0, slash);

    static std::vector<std::pair<std::string, std::string>> cache;
    for (const auto& [d, art] : cache)
        if (d == dir) return art;

    struct Ctx { std::string best; int bestScore = 0; int images = 0;
                 std::string lone; };
    Ctx ctx;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* ud, const char* dname, const char* fname) {
            Ctx* c = (Ctx*)ud;
            std::string name = fname;
            std::string low = name;
            for (char& ch : low) ch = (char)std::tolower((unsigned char)ch);
            size_t dot = low.rfind('.');
            if (dot == std::string::npos) return SDL_ENUM_CONTINUE;
            std::string ext = low.substr(dot);
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
                ext != ".bmp")
                return SDL_ENUM_CONTINUE;
            std::string full = std::string(dname) +
                               (dname[0] && dname[strlen(dname) - 1] != '/'
                                    ? "/" : "") + name;
            ++c->images;
            c->lone = full;
            std::string stem = low.substr(0, dot);
            static const char* kNames[] = {"cover", "folder", "front",
                                           "albumart", "album", "art"};
            for (int i = 0; i < 6; ++i)
                if (stem == kNames[i] && 6 - i > c->bestScore) {
                    c->bestScore = 6 - i;
                    c->best = full;
                }
            return SDL_ENUM_CONTINUE;
        },
        &ctx);

    std::string art = !ctx.best.empty() ? ctx.best
                    : ctx.images == 1   ? ctx.lone
                                        : "";
    cache.push_back({dir, art});
    return art;
}

static void loadArt(const Track& t) {
    if (g.art) { SDL_DestroyTexture(g.art); g.art = nullptr; }
    int w = 0, h = 0;
    std::vector<uint8_t> px = tags::readArtRGBA(t.path, w, h);
    if (px.empty()) {
        std::string folder = folderArtFor(t.path);
        if (!folder.empty()) px = tags::loadImageRGBA(folder, w, h);
    }
    if (px.empty()) return;
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32,
                                           px.data(), w * 4);
    if (!s) return;
    g.art = SDL_CreateTextureFromSurface(g.ren, s);
    SDL_DestroySurface(s);
    if (g.art) SDL_SetTextureScaleMode(g.art, SDL_SCALEMODE_LINEAR);
}

static void playCurrent() {
    const Track* t = currentTrack();
    if (!t) return;
    if (!playback::play(t->path))
        std::fprintf(stderr, "cannot play %s\n", t->path.c_str());
    loadArt(*t);
}

void nextTrack() {
    if (g.queue.empty()) return;
    if (g.qpos + 1 < (int)g.queue.size()) { ++g.qpos; playCurrent(); }
    else playback::stop();  // end of queue
}

void prevTrack() {
    if (g.queue.empty()) return;
    if (playback::position() > 3.0) { playback::seekTo(0); return; }
    if (g.qpos > 0) --g.qpos;
    playCurrent();
}

void playOrResume() {
    if (playback::active()) { playback::setPaused(false); return; }
    if (currentTrack()) { playCurrent(); return; }  // stopped: restart track
    std::vector<int> all = g.lib.songsByTitle();    // fresh start: play all
    if (all.empty()) return;
    g.queue = all;
    g.qpos = 0;
    playCurrent();
}

void playPauseToggle() {
    if (playback::active()) playback::setPaused(!playback::paused());
    else playOrResume();
}

static void pushNowPlaying() {
    Page p;
    p.view = View::NOW_PLAYING;
    p.title = "Now Playing";
    g.nav.push_back(p);
}

// Start playing row `idx` of `page`, queueing every track row around it.
static void playFromPage(const Page& page, int idx) {
    g.queue.clear();
    int pos = 0;
    for (int i = 0; i < (int)page.rows.size(); ++i) {
        if (page.rows[i].track < 0) continue;
        if (i == idx) pos = (int)g.queue.size();
        g.queue.push_back(page.rows[i].track);
    }
    g.qpos = pos;
    playCurrent();
    pushNowPlaying();
}

void activateRow(Page& page, int idx) {
    if (idx < 0 || idx >= (int)page.rows.size()) return;
    const Row& row = page.rows[idx];
    if (row.track >= 0) { playFromPage(page, idx); return; }

    switch (row.action) {
        case A_MUSIC:   g.nav.push_back(pageMusic()); break;
        case A_ARTISTS: g.nav.push_back(pageArtists()); break;
        case A_ALBUMS:  g.nav.push_back(pageAlbums()); break;
        case A_YEARS:   g.nav.push_back(pageYears()); break;
        case A_SONGS:
            g.nav.push_back(pageSongs("Songs", g.lib.songsByTitle()));
            break;
        case A_ARTIST:
            g.nav.push_back(pageArtistAlbums(row.arg));
            break;
        case A_ARTIST_ALL:
            g.nav.push_back(pageSongs(row.arg, g.lib.tracksOfArtist(row.arg)));
            break;
        case A_ALBUM:
            g.nav.push_back(pageSongs(row.arg, g.lib.tracksOfAlbum(row.arg)));
            break;
        case A_ALBUM_OF_ARTIST: {
            size_t nl = row.arg.find('\n');
            std::string artist = row.arg.substr(0, nl);
            std::string album  = row.arg.substr(nl + 1);
            g.nav.push_back(pageSongs(album, g.lib.tracksOfAlbum(album, artist)));
            break;
        }
        case A_YEAR: {
            std::string label = row.year ? std::to_string(row.year) : "Unknown";
            g.nav.push_back(pageSongs(label, g.lib.tracksOfYear(row.year)));
            break;
        }
        case A_SHUFFLE: {
            std::vector<int> all = g.lib.songsByTitle();
            if (all.empty()) break;
            static std::mt19937 rng{std::random_device{}()};
            std::shuffle(all.begin(), all.end(), rng);
            g.queue = all;
            g.qpos = 0;
            playCurrent();
            pushNowPlaying();
            break;
        }
        case A_NOW_PLAYING:
            if (currentTrack()) pushNowPlaying();
            break;
    }
}

// ---------------------------------------------------------------- input

void goBack() {
    if (g.nav.size() > 1) g.nav.pop_back();
}

void scrollStep(int dir) {
    Page& p = g.nav.back();
    if (p.view == View::NOW_PLAYING) {
        playback::setVolume(playback::volume() + (dir > 0 ? 0.05f : -0.05f));
        g.volOverlayUntil = SDL_GetTicks() + 1200;
        return;
    }
    if (p.rows.empty()) return;
    p.cursor = std::clamp(p.cursor + dir, 0, (int)p.rows.size() - 1);
}

void selectPress() {
    Page& p = g.nav.back();
    if (p.view == View::LIST) activateRow(p, p.cursor);
}

void handleKey(SDL_Keycode k) {
    bool nowPlaying = g.nav.back().view == View::NOW_PLAYING;
    switch (k) {
        case SDLK_UP:    scrollStep(-1); break;
        case SDLK_DOWN:  scrollStep(+1); break;
        case SDLK_RETURN: selectPress(); break;
        case SDLK_RIGHT:
            if (nowPlaying) playback::seekTo(playback::position() + 5);
            else selectPress();
            break;
        case SDLK_LEFT:
            if (nowPlaying) playback::seekTo(playback::position() - 5);
            else goBack();
            break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: goBack(); break;
        case SDLK_SPACE: playPauseToggle(); break;
        case SDLK_PERIOD: case SDLK_N: nextTrack(); break;
        case SDLK_COMMA:  case SDLK_P: prevTrack(); break;
        case SDLK_Q: g.quit = true; break;
        default: break;
    }
}
