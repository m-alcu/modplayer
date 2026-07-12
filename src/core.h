#pragma once

// ---------------------------------------------------------------------------
// Shared player core: the library/navigation/queue state, the actions every
// skin triggers (scroll, select, back, next/prev), and the small drawing
// toolkit (8x8 text, filled shapes, gradients) skins are built from.
//
// A Skin supplies its logical canvas size, a render function and a mouse
// handler; keyboard input and the main loop live in main.cpp and behave the
// same in every skin.
// ---------------------------------------------------------------------------

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "library.h"

// ------------------------------------------------------------------ colors

struct Col { Uint8 r, g, b; };
constexpr Col C_TEXT   {28, 28, 32};
constexpr Col C_GRAY   {110, 116, 126};
constexpr Col C_WHITE  {255, 255, 255};
constexpr Col C_SEL_TOP{110, 168, 224};               // iPod blue gradient
constexpr Col C_SEL_BOT{40, 96, 168};

// -------------------------------------------------------------- primitives

void setCol(SDL_Renderer* r, Col c, Uint8 a = 255);
void fillR(SDL_Renderer* r, float x, float y, float w, float h);
void fillCircle(SDL_Renderer* r, float cx, float cy, float rad, Col c,
                Uint8 a = 255);
void fillRing(SDL_Renderer* r, float cx, float cy, float rOut, float rIn,
              Col c);
void fillRoundRect(SDL_Renderer* r, SDL_FRect rc, float rad, Col c);
void vGradient(SDL_Renderer* r, SDL_FRect rc, Col top, Col bot);
void fillTriangle(SDL_Renderer* r, float x0, float y0, float x1, float y1,
                  float x2, float y2, Col c);

// -------------------------------------------------------------------- text

constexpr int ADV = 7;  // glyph advance in font pixels (glyphs are ~6px wide)

std::string foldAscii(const std::string& utf8);
int  textW(const std::string& s, int scale);
void drawTextRaw(SDL_Renderer* r, const std::string& ascii, float x, float y,
                 int scale, Col c, bool bold = false);
void drawText(SDL_Renderer* r, const std::string& utf8, float x, float y,
              int scale, Col c, float maxW = 0, bool bold = false);
void drawTextCentered(SDL_Renderer* r, const std::string& utf8, float cx,
                      float y, int scale, Col c, bool bold = false);
std::string fmtTime(double s);

// Placeholder cover: gradient with a big musical note (skins share it).
void drawNoArt(SDL_Renderer* r, SDL_FRect rc, Col top, Col bot, Col note);

// --------------------------------------------------------------- app state

enum class View { LIST, NOW_PLAYING };

struct Row {
    std::string label;
    bool arrow = false;   // shows the ">" submenu chevron
    int track = -1;       // >=0: playable track (library index)
    int action = 0;       // submenu id
    std::string arg;      // artist/album name for submenus
    int year = -1;
};

struct Page {
    View view = View::LIST;
    std::string title;
    std::vector<Row> rows;
    int cursor = 0, scroll = 0;
};

// A skin: logical canvas + renderer + mouse handler. Keyboard is shared.
struct Skin {
    const char* name;
    int w, h;      // logical canvas size
    int scale;     // default window scale (window opens at w*scale x h*scale)
    void (*render)(SDL_Renderer* r);
    void (*event)(const SDL_Event& e);
};
extern const Skin kSkinNano, kSkinClassic, kSkinWinamp, kSkinFoobar,
    kSkinZune, kSkinCassette, kSkinVinyl, kSkinCar, kSkinTerm, kSkinMini,
    kSkinTracker;

struct App {
    SDL_Window*   win = nullptr;
    SDL_Renderer* ren = nullptr;
    Library       lib;
    const Skin*   skin = nullptr;
    std::vector<Page> nav;      // navigation stack; back() is on screen

    std::vector<int> queue;     // play queue (library indexes)
    int qpos = -1;
    SDL_Texture* art = nullptr; // cover of the current track (or null)

    Uint64 volOverlayUntil = 0; // show volume bar in Now Playing until then
    bool   quit = false;

    // click-wheel drag state (nano skin)
    bool  wheelDown = false;
    bool  wheelMoved = false;
    float wheelAngle = 0, wheelAccum = 0;
};
extern App g;

// ------------------------------------------------------------- navigation

const Track* currentTrack();
Page pageMain();
Page pageMusic();
Page pageSongs(const std::string& title, const std::vector<int>& idxs);

void activateRow(Page& page, int idx);
void goBack();
void scrollStep(int dir);       // one wheel detent / arrow key
void selectPress();
void handleKey(SDL_Keycode k);
void nextTrack();
void prevTrack();

// Play button semantics: resume when paused, restart the current queue track
// when stopped, or queue every song and start when nothing was ever played.
void playOrResume();
void playPauseToggle();
