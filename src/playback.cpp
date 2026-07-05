#include "playback.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "pocketmod.h"  // implementation compiled in pocketmod_impl.c

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace playback {
namespace {

bool              g_ok = false;
SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec     g_devSpec{};
SDL_AudioStream*  g_stream = nullptr;

Uint8*        g_pcm = nullptr;    // SDL_malloc'd interleaved samples
Uint32        g_pcmLen = 0;       // bytes
Uint32        g_cursor = 0;       // bytes already fed to the stream
SDL_AudioSpec g_spec{};           // format of g_pcm
int           g_bytesPerSec = 0;
bool          g_paused = false;
float         g_volume = 0.8f;

// ----------------------------------------------------------------- modules
// Tracker module rendering, whole song to float stereo PCM at load time.
// A module has no intrinsic length — the pattern order can loop forever —
// so the track is one full pass of the song (with a hard cap for
// pathological modules that never end).
//
// The system libopenmpt handles every format (MOD/XM/S3M/IT and friends).
// It is loaded with dlopen so there is no build-time dependency; without it
// the vendored pocketmod still plays ProTracker MODs, and only XM/S3M/IT
// files fail to load.

constexpr size_t kCapSeconds = 15 * 60;  // one pass, at most 15 minutes

// Mirror of the (stable) C ABI from libopenmpt/libopenmpt.h, so no dev
// headers are needed. Modules are opaque; log/error callbacks stay NULL.
struct MptApi {
    void*  (*create)(const void* data, size_t size, void* logfn, void* loguser,
                     void* errfn, void* erruser, int* error,
                     const char** errmsg, const void* ctls);
    void   (*destroy)(void* mod);
    int    (*setRepeat)(void* mod, int32_t count);
    size_t (*readF32)(void* mod, int32_t rate, size_t frames, float* out);
    bool ok;
};

const MptApi& mpt() {
    static MptApi api = [] {
        MptApi a{};
#ifndef _WIN32
        void* so = dlopen("libopenmpt.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!so) so = dlopen("libopenmpt.so", RTLD_NOW | RTLD_LOCAL);
        if (!so) so = dlopen("libopenmpt.0.dylib", RTLD_NOW | RTLD_LOCAL);
        if (so) {
            a.create    = (decltype(a.create))
                dlsym(so, "openmpt_module_create_from_memory2");
            a.destroy   = (decltype(a.destroy))
                dlsym(so, "openmpt_module_destroy");
            a.setRepeat = (decltype(a.setRepeat))
                dlsym(so, "openmpt_module_set_repeat_count");
            a.readF32   = (decltype(a.readF32))
                dlsym(so, "openmpt_module_read_interleaved_float_stereo");
            a.ok = a.create && a.destroy && a.setRepeat && a.readF32;
        }
#endif
        return a;
    }();
    return api;
}

bool renderOpenmpt(const std::vector<uint8_t>& file, int rate,
                   std::vector<uint8_t>& pcm) {
    const MptApi& api = mpt();
    if (!api.ok) return false;
    void* mod = api.create(file.data(), file.size(), nullptr, nullptr,
                           nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!mod) return false;
    api.setRepeat(mod, 0);  // one pass; a 0-frame read marks the song end

    float buf[4096 * 2];
    size_t frames = 0;
    const size_t capFrames = (size_t)rate * kCapSeconds;
    while (frames < capFrames) {
        size_t n = api.readF32(mod, rate, 4096, buf);
        if (n == 0) break;
        const uint8_t* p = (const uint8_t*)buf;
        pcm.insert(pcm.end(), p, p + n * 2 * sizeof(float));
        frames += n;
    }
    api.destroy(mod);
    return !pcm.empty();
}

bool renderPocketmod(const std::vector<uint8_t>& file, int rate,
                     std::vector<uint8_t>& pcm) {
    pocketmod_context ctx;
    if (!pocketmod_init(&ctx, file.data(), (int)file.size(), rate))
        return false;
    const size_t cap = (size_t)rate * 2 * sizeof(float) * kCapSeconds;
    uint8_t buf[16384];
    while (pocketmod_loop_count(&ctx) == 0 && pcm.size() < cap) {
        int n = pocketmod_render(&ctx, buf, sizeof buf);
        if (n <= 0) break;
        pcm.insert(pcm.end(), buf, buf + n);
    }
    return !pcm.empty();
}

bool decodeMod(const std::string& path) {
    std::vector<uint8_t> file;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::rewind(f);
        if (n > 0) {
            file.resize((size_t)n);
            if (std::fread(file.data(), 1, file.size(), f) != file.size())
                file.clear();
        }
        std::fclose(f);
    }
    if (file.empty()) return false;

    // Render at the device rate when we have one, so SDL never resamples.
    const int rate = (g_ok && g_devSpec.freq >= 8000) ? g_devSpec.freq : 44100;
    std::vector<uint8_t> pcm;
    if (!renderOpenmpt(file, rate, pcm) &&
        !renderPocketmod(file, rate, pcm)) {
        std::fprintf(stderr, mpt().ok
                         ? "playback: %s: not a playable module\n"
                         : "playback: %s: not a ProTracker MOD (XM/S3M/IT "
                           "need the system libopenmpt)\n",
                     path.c_str());
        return false;
    }

    g_spec.format   = SDL_AUDIO_F32;
    g_spec.channels = 2;
    g_spec.freq     = rate;
    g_pcmLen = (Uint32)pcm.size();
    g_pcm = (Uint8*)SDL_malloc(g_pcmLen);
    if (g_pcm) std::memcpy(g_pcm, pcm.data(), g_pcmLen);
    return g_pcm != nullptr;
}

// Feed up to ~1 second ahead of the device.
void topUp() {
    if (!g_stream || g_paused || g_cursor >= g_pcmLen) return;
    const Uint32 ahead = (Uint32)g_bytesPerSec;
    while (g_cursor < g_pcmLen &&
           SDL_GetAudioStreamQueued(g_stream) < (int)ahead) {
        Uint32 chunk = std::min<Uint32>(g_bytesPerSec / 4, g_pcmLen - g_cursor);
        if (!SDL_PutAudioStreamData(g_stream, g_pcm + g_cursor, (int)chunk))
            break;
        g_cursor += chunk;
    }
    if (g_cursor >= g_pcmLen) SDL_FlushAudioStream(g_stream);
}

void freeTrack() {
    if (g_stream) {
        SDL_UnbindAudioStream(g_stream);
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
    }
    if (g_pcm) SDL_free(g_pcm);
    g_pcm = nullptr;
    g_pcmLen = g_cursor = 0;
}

}  // namespace

bool init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "playback: SDL_INIT_AUDIO failed: %s\n",
                     SDL_GetError());
        return false;
    }
    g_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!g_dev) {
        std::fprintf(stderr, "playback: no audio device: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_GetAudioDeviceFormat(g_dev, &g_devSpec, nullptr);
    g_ok = true;
    return true;
}

bool ready() { return g_ok; }

void shutdown() {
    freeTrack();
    if (g_dev) SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    if (g_ok) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g_ok = false;
}

bool play(const std::string& path) {
    freeTrack();
    if (!decodeMod(path)) return false;
    g_bytesPerSec = g_spec.freq * g_spec.channels *
                    (SDL_AUDIO_BITSIZE(g_spec.format) / 8);
    g_paused = false;
    if (!g_ok) return true;  // decoded fine; silent without a device

    g_stream = SDL_CreateAudioStream(&g_spec, &g_devSpec);
    if (!g_stream) return true;
    SDL_SetAudioStreamGain(g_stream, g_volume);
    if (!SDL_BindAudioStream(g_dev, g_stream)) {
        SDL_DestroyAudioStream(g_stream);
        g_stream = nullptr;
        return true;
    }
    // Pausing pauses the *device*; a stream destroyed while paused would
    // otherwise leave the device silent for every track loaded after it.
    SDL_ResumeAudioStreamDevice(g_stream);
    topUp();
    return true;
}

void stop() { freeTrack(); }

void setPaused(bool p) {
    if (g_paused == p) return;
    g_paused = p;
    if (!g_stream) return;
    if (p) SDL_PauseAudioStreamDevice(g_stream);
    else   SDL_ResumeAudioStreamDevice(g_stream);
}

bool paused() { return g_paused; }
bool active() { return g_pcm != nullptr; }

bool finished() {
    if (!g_pcm) return false;
    if (!g_stream) return true;  // no device: pretend it ended instantly
    return g_cursor >= g_pcmLen &&
           SDL_GetAudioStreamQueued(g_stream) == 0 &&
           SDL_GetAudioStreamAvailable(g_stream) == 0;
}

double duration() {
    return g_bytesPerSec > 0 ? (double)g_pcmLen / g_bytesPerSec : 0.0;
}

double position() {
    if (!g_pcm || g_bytesPerSec <= 0) return 0.0;
    Uint32 queued = g_stream ? (Uint32)SDL_GetAudioStreamQueued(g_stream) : 0;
    Uint32 played = g_cursor > queued ? g_cursor - queued : 0;
    return (double)played / g_bytesPerSec;
}

void seekTo(double secs) {
    if (!g_pcm || g_bytesPerSec <= 0) return;
    secs = std::clamp(secs, 0.0, duration());
    Uint32 frameBytes = (Uint32)(g_spec.channels *
                                 (SDL_AUDIO_BITSIZE(g_spec.format) / 8));
    Uint32 off = (Uint32)(secs * g_bytesPerSec);
    off -= off % frameBytes;  // stay frame-aligned
    g_cursor = std::min(off, g_pcmLen);
    if (g_stream) {
        SDL_ClearAudioStream(g_stream);
        topUp();
    }
}

int channels()   { return g_pcm ? g_spec.channels : 0; }
int sampleRate() { return g_pcm ? g_spec.freq : 0; }

int visSamples(float* out, int n) {
    if (!g_pcm || g_bytesPerSec <= 0) return 0;
    const int bps = SDL_AUDIO_BITSIZE(g_spec.format) / 8;
    const int ch = g_spec.channels;
    if (bps <= 0 || ch <= 0) return 0;
    Uint32 queued = g_stream ? (Uint32)SDL_GetAudioStreamQueued(g_stream) : 0;
    Uint32 played = g_cursor > queued ? g_cursor - queued : 0;
    Uint32 frame = played / (Uint32)(bps * ch);
    Uint32 total = g_pcmLen / (Uint32)(bps * ch);
    int got = 0;
    for (; got < n && frame < total; ++got, ++frame) {
        const Uint8* p = g_pcm + (size_t)frame * bps * ch;
        float acc = 0;
        for (int c = 0; c < ch; ++c) {
            if (g_spec.format == SDL_AUDIO_F32)
                acc += ((const float*)p)[c];
            else if (g_spec.format == SDL_AUDIO_S16)
                acc += ((const Sint16*)p)[c] / 32768.0f;
            else if (g_spec.format == SDL_AUDIO_U8)
                acc += (((const Uint8*)p)[c] - 128) / 128.0f;
            else
                return 0;
        }
        out[got] = acc / ch;
    }
    return got;
}

void setVolume(float v) {
    g_volume = std::clamp(v, 0.0f, 1.0f);
    if (g_stream) SDL_SetAudioStreamGain(g_stream, g_volume);
}

float volume() { return g_volume; }

void update() { topUp(); }

}  // namespace playback
