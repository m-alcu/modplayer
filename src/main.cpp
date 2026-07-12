// ---------------------------------------------------------------------------
// modplayer — a tracker module player (MOD/XM/S3M/IT): SDL3 for
// video/audio/input, libopenmpt (dlopen) or the vendored pocketmod for
// module rendering, stb_image for folder cover art.
//
// Browse the library by Artists / Albums / Songs / Years; the Now Playing
// screen shows folder cover art when present. Eleven skins (cycle with F2 or
// pick with --skin):
//   nano     Apple iPod nano look with a working click wheel (default)
//   classic  Apple iPod Classic: split-screen menu + chrome click wheel
//   winamp   Winamp-classic look: LCD digits, spectrum bars, playlist pane
//   foobar   foobar2000: columned playlist, toolbar, status bar
//   zune     Zune Metro: flat black, oversized lowercase type
//   cassette animated compact cassette (reels spin, tape winds)
//   vinyl    turntable: spinning record, tonearm tracks progress
//   car      90s CD head unit: amber VFD, 7-seg clock, chunky buttons
//   term     terminal player in the cmus mould, green on black
//   mini     neutral 320x240 landscape UI for small screens (Raspberry Pi);
//            add --fullscreen to fill the display
//   tracker  FastTracker II / Scream Tracker mould: per-channel scopes and
//            the pattern editor scrolling with the music (needs libopenmpt)
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "core.h"
#include "library.h"
#include "playback.h"

namespace {

const Skin* kSkins[] = {&kSkinNano,     &kSkinClassic, &kSkinWinamp,
                        &kSkinFoobar,   &kSkinZune,    &kSkinCassette,
                        &kSkinVinyl,    &kSkinCar,     &kSkinTerm,
                        &kSkinMini,     &kSkinTracker};
constexpr int kNumSkins = (int)(sizeof kSkins / sizeof kSkins[0]);

const Skin* skinByName(const std::string& name) {
    for (const Skin* s : kSkins)
        if (name == s->name) return s;
    return nullptr;
}

void applySkin(const Skin* s, bool resizeWindow) {
    g.skin = s;
    if (resizeWindow)
        SDL_SetWindowSize(g.win, s->w * s->scale, s->h * s->scale);
    SDL_SetRenderLogicalPresentation(g.ren, s->w, s->h,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

void render() {
    g.skin->render(g.ren);
    SDL_RenderPresent(g.ren);
}

void handleEvent(SDL_Event& e) {
    SDL_ConvertEventToRenderCoordinates(g.ren, &e);
    switch (e.type) {
        case SDL_EVENT_QUIT: g.quit = true; break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_F2) {  // cycle skins
                int i = 0;
                for (int k = 0; k < kNumSkins; ++k)
                    if (kSkins[k] == g.skin) i = k;
                applySkin(kSkins[(i + 1) % kNumSkins], true);
            } else {
                handleKey(e.key.key);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            scrollStep(e.wheel.y > 0 ? -1 : +1);
            break;
        default:
            g.skin->event(e);
            break;
    }
}

// ----------------------------------------------------------------- modes

int selftest() {
    std::printf("library: %d tracks in %s\n", (int)g.lib.tracks().size(),
                g.lib.root().c_str());
    for (const std::string& a : g.lib.artists()) {
        std::printf("artist: %s\n", a.c_str());
        for (const std::string& al : g.lib.albumsOf(a)) {
            std::printf("  album: %s\n", al.c_str());
            for (int i : g.lib.tracksOfAlbum(al, a)) {
                const Track& t = g.lib.tracks()[i];
                std::printf("    %2d. %s (%d)%s\n", t.meta.track,
                            t.title.c_str(), t.meta.year,
                            t.meta.hasArt ? " [art]" : "");
            }
        }
    }
    std::printf("years:");
    for (int y : g.lib.years()) std::printf(" %d", y);
    std::printf("\n");
    return 0;
}

void saveShot(const std::string& path) {
    SDL_Surface* s = SDL_RenderReadPixels(g.ren, nullptr);
    if (s) {
        SDL_SaveBMP(s, path.c_str());
        SDL_DestroySurface(s);
        std::printf("wrote %s\n", path.c_str());
    }
}

// Render key screens of the current skin to BMPs (headless-friendly).
int shotMode(const std::string& prefix) {
    std::string base = prefix + "-" + g.skin->name;
    render(); saveShot(base + "-main.bmp");

    g.nav.push_back(pageMusic());
    render(); saveShot(base + "-music.bmp");

    g.nav.push_back(pageSongs("Songs", g.lib.songsByTitle()));
    if (!g.nav.back().rows.empty()) {
        // Prefer a track with embedded art so the shot exercises that path.
        Page& songs = g.nav.back();
        for (int i = 0; i < (int)songs.rows.size(); ++i)
            if (songs.rows[i].track >= 0 &&
                g.lib.tracks()[songs.rows[i].track].meta.hasArt) {
                songs.cursor = i;
                break;
            }
        render(); saveShot(base + "-songs.bmp");
        activateRow(g.nav.back(), g.nav.back().cursor);
        playback::setPaused(true);
        // mid-song, so position-dependent visuals show something typical
        playback::seekTo(playback::duration() * 0.4);
        render(); saveShot(base + "-now.bmp");
    } else {
        render(); saveShot(base + "-songs.bmp");
    }
    return 0;
}

// ------------------------------------------------------------------ main

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

std::string defaultMusicDir() {
    const char* home = SDL_getenv("HOME");
    if (home) {
        std::string music = std::string(home) + "/Music";
        SDL_PathInfo info;
        if (SDL_GetPathInfo(music.c_str(), &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY)
            return music;
    }
    return std::string(ASSET_ROOT) + "/assets/sounds";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir, shotPrefix;
    const Skin* skin = &kSkinNano;
    bool doSelftest = false, fullscreen = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--selftest") doSelftest = true;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--shot" && i + 1 < argc) shotPrefix = argv[++i];
        else if (a == "--skin" && i + 1 < argc) {
            skin = skinByName(argv[++i]);
            if (!skin) {
                std::fprintf(stderr, "unknown skin '%s' (", argv[i]);
                for (int k = 0; k < kNumSkins; ++k)
                    std::fprintf(stderr, "%s%s", k ? ", " : "",
                                 kSkins[k]->name);
                std::fprintf(stderr, ")\n");
                return 1;
            }
        } else dir = a;
    }
    bool dirIsDefault = dir.empty();
    if (dir.empty()) dir = defaultMusicDir();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int n = g.lib.scan(dir);
    if (n == 0 && dirIsDefault) {
        // Empty ~/Music: fall back to the bundled demo tracks so the player
        // always has something to browse out of the box.
        dir = std::string(ASSET_ROOT) + "/assets/sounds";
        n = g.lib.scan(dir);
    }
    std::printf("modplayer: %d modules found in %s\n", n, dir.c_str());
    if (doSelftest) return selftest();

    g.win = SDL_CreateWindow("modplayer", skin->w * skin->scale,
                             skin->h * skin->scale, SDL_WINDOW_RESIZABLE);
    if (!g.win) {
        std::fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    if (fullscreen) SDL_SetWindowFullscreen(g.win, true);
    g.ren = SDL_CreateRenderer(g.win, nullptr);
    if (!g.ren) {
        std::fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    applySkin(skin, false);

    playback::init();  // ok to fail: UI still browses
    g.nav.push_back(pageMain());

    if (!shotPrefix.empty()) {
        int rc = shotMode(shotPrefix);
        playback::shutdown();
        SDL_Quit();
        return rc;
    }

    while (!g.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);

        playback::update();
        if (playback::ready() && playback::active() && playback::finished())
            nextTrack();

        render();
        SDL_Delay(16);
    }

    playback::shutdown();
    if (g.art) SDL_DestroyTexture(g.art);
    SDL_DestroyRenderer(g.ren);
    SDL_DestroyWindow(g.win);
    SDL_Quit();
    return 0;
}
