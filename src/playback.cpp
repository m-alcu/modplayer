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

// Mirror of the (stable) C ABI from libopenmpt/libopenmpt.h and
// libopenmpt_ext.h, so no dev headers are needed. Modules are opaque;
// log/error callbacks stay NULL. The introspection symbols (numChannels…)
// feed the tracker skin; the ext/interactive ones render per-channel scopes
// by muting all other channels — both groups are optional, playback works
// without them.
struct MptApi {
    void*  (*create)(const void* data, size_t size, void* logfn, void* loguser,
                     void* errfn, void* erruser, int* error,
                     const char** errmsg, const void* ctls);
    void   (*destroy)(void* mod);
    int    (*setRepeat)(void* mod, int32_t count);
    size_t (*readF32)(void* mod, int32_t rate, size_t frames, float* out);

    // introspection (all optional)
    int32_t (*numChannels)(void* mod);
    int32_t (*numOrders)(void* mod);
    int32_t (*orderPattern)(void* mod, int32_t order);
    int32_t (*patternRows)(void* mod, int32_t pattern);
    int32_t (*numPatterns)(void* mod);
    int32_t (*numInstruments)(void* mod);
    int32_t (*numSamples)(void* mod);
    const char* (*instrumentName)(void* mod, int32_t i);
    const char* (*sampleName)(void* mod, int32_t i);
    const char* (*metadata)(void* mod, const char* key);
    const char* (*formatCell)(void* mod, int32_t pattern, int32_t row,
                              int32_t channel, size_t width, int pad);
    void    (*freeString)(const char* s);
    int32_t (*curOrder)(void* mod);
    int32_t (*curRow)(void* mod);
    int32_t (*curSpeed)(void* mod);
    int32_t (*curTempo)(void* mod);
    double  (*setPosSeconds)(void* mod, double s);
    size_t  (*readMono)(void* mod, int32_t rate, size_t n, float* out);

    // module_ext + interactive interface (per-channel mute, all optional)
    void* (*extCreate)(const void* data, size_t size, void* logfn,
                       void* loguser, void* errfn, void* erruser, int* error,
                       const char** errmsg, const void* ctls);
    void  (*extDestroy)(void* ext);
    void* (*extGetModule)(void* ext);
    int   (*extGetInterface)(void* ext, const char* id, void* iface,
                             size_t size);
    bool ok;      // core rendering symbols found
    bool okInfo;  // pattern introspection symbols found
    bool okExt;   // per-channel scope symbols found
};

// libopenmpt_ext.h "interactive" interface, ABI-frozen since 0.3.
struct MptInteractive {
    int    (*setSpeed)(void* ext, int32_t speed);
    int    (*setTempo)(void* ext, int32_t tempo);
    int    (*setTempoFactor)(void* ext, double f);
    double (*getTempoFactor)(void* ext);
    int    (*setPitchFactor)(void* ext, double f);
    double (*getPitchFactor)(void* ext);
    int    (*setGlobalVolume)(void* ext, double v);
    double (*getGlobalVolume)(void* ext);
    int    (*setChannelVolume)(void* ext, int32_t ch, double v);
    double (*getChannelVolume)(void* ext, int32_t ch);
    int    (*setChannelMute)(void* ext, int32_t ch, int mute);
    int    (*getChannelMute)(void* ext, int32_t ch);
    int    (*setInstrumentMute)(void* ext, int32_t ins, int mute);
    int    (*getInstrumentMute)(void* ext, int32_t ins);
    int32_t (*playNote)(void* ext, int32_t ins, int32_t note, double vol,
                        double pan);
    int    (*stopNote)(void* ext, int32_t ch);
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

            a.numChannels    = (decltype(a.numChannels))
                dlsym(so, "openmpt_module_get_num_channels");
            a.numOrders      = (decltype(a.numOrders))
                dlsym(so, "openmpt_module_get_num_orders");
            a.orderPattern   = (decltype(a.orderPattern))
                dlsym(so, "openmpt_module_get_order_pattern");
            a.patternRows    = (decltype(a.patternRows))
                dlsym(so, "openmpt_module_get_pattern_num_rows");
            a.numPatterns    = (decltype(a.numPatterns))
                dlsym(so, "openmpt_module_get_num_patterns");
            a.numInstruments = (decltype(a.numInstruments))
                dlsym(so, "openmpt_module_get_num_instruments");
            a.numSamples     = (decltype(a.numSamples))
                dlsym(so, "openmpt_module_get_num_samples");
            a.instrumentName = (decltype(a.instrumentName))
                dlsym(so, "openmpt_module_get_instrument_name");
            a.sampleName     = (decltype(a.sampleName))
                dlsym(so, "openmpt_module_get_sample_name");
            a.metadata       = (decltype(a.metadata))
                dlsym(so, "openmpt_module_get_metadata");
            a.formatCell     = (decltype(a.formatCell))
                dlsym(so, "openmpt_module_format_pattern_row_channel");
            a.freeString     = (decltype(a.freeString))
                dlsym(so, "openmpt_free_string");
            a.curOrder       = (decltype(a.curOrder))
                dlsym(so, "openmpt_module_get_current_order");
            a.curRow         = (decltype(a.curRow))
                dlsym(so, "openmpt_module_get_current_row");
            a.curSpeed       = (decltype(a.curSpeed))
                dlsym(so, "openmpt_module_get_current_speed");
            a.curTempo       = (decltype(a.curTempo))
                dlsym(so, "openmpt_module_get_current_tempo");
            a.okInfo = a.numChannels && a.numOrders && a.orderPattern &&
                       a.patternRows && a.numPatterns && a.formatCell &&
                       a.freeString && a.curOrder && a.curRow && a.curSpeed &&
                       a.curTempo;

            a.setPosSeconds  = (decltype(a.setPosSeconds))
                dlsym(so, "openmpt_module_set_position_seconds");
            a.readMono       = (decltype(a.readMono))
                dlsym(so, "openmpt_module_read_float_mono");
            a.extCreate      = (decltype(a.extCreate))
                dlsym(so, "openmpt_module_ext_create_from_memory");
            a.extDestroy     = (decltype(a.extDestroy))
                dlsym(so, "openmpt_module_ext_destroy");
            a.extGetModule   = (decltype(a.extGetModule))
                dlsym(so, "openmpt_module_ext_get_module");
            a.extGetInterface = (decltype(a.extGetInterface))
                dlsym(so, "openmpt_module_ext_get_interface");
            a.okExt = a.setPosSeconds && a.readMono && a.extCreate &&
                      a.extDestroy && a.extGetModule && a.extGetInterface;
        }
#endif
        return a;
    }();
    return api;
}

// --------------------------------------------------- tracker-data capture
// Everything the tracker skin shows is captured at load time: the pattern
// cells (static), a row timeline recorded while the song renders (audio
// frame -> order/row/speed/tempo, mapped back from the PCM playhead later),
// and one low-rate solo render per channel for the oscilloscopes.

constexpr int kScopeRate = 11025;  // Hz; plenty for a few-pixels-tall scope

struct RowEvent {
    uint32_t frame;  // first PCM frame of this row (at the render rate)
    int16_t order, pattern, row, speed, tempo;
};

int                      g_tkChannels = 0;
std::vector<RowEvent>    g_tkTimeline;
std::vector<int>         g_tkOrderPat;   // order list -> pattern index
std::vector<int>         g_tkPatRows;    // pattern -> row count
std::vector<std::vector<std::string>> g_tkCells;  // [pattern][row*nch+ch]
std::string              g_tkTitle;
std::vector<std::string> g_tkInstr;
std::vector<std::vector<int8_t>> g_tkScope;  // [ch] mono @ kScopeRate

void clearTrackerData() {
    g_tkChannels = 0;
    g_tkTimeline.clear();
    g_tkOrderPat.clear();
    g_tkPatRows.clear();
    g_tkCells.clear();
    g_tkTitle.clear();
    g_tkInstr.clear();
    g_tkScope.clear();
}

std::string mptString(const char* s) {
    std::string out = s ? s : "";
    if (s) mpt().freeString(s);
    return out;
}

// Static module data: order list, pattern cells, title, instrument names.
void captureModuleInfo(void* mod) {
    const MptApi& api = mpt();
    if (!api.okInfo) return;
    g_tkChannels = api.numChannels(mod);
    if (g_tkChannels <= 0) { g_tkChannels = 0; return; }

    int orders = api.numOrders(mod);
    for (int o = 0; o < orders; ++o)
        g_tkOrderPat.push_back(api.orderPattern(mod, o));

    int patterns = api.numPatterns(mod);
    g_tkPatRows.resize(patterns, 0);
    g_tkCells.resize(patterns);
    for (int p = 0; p < patterns; ++p) {
        int rows = api.patternRows(mod, p);
        g_tkPatRows[p] = rows;
        g_tkCells[p].reserve((size_t)rows * g_tkChannels);
        for (int row = 0; row < rows; ++row)
            for (int ch = 0; ch < g_tkChannels; ++ch)
                g_tkCells[p].push_back(
                    mptString(api.formatCell(mod, p, row, ch, 14, 1)));
    }

    if (api.metadata) g_tkTitle = mptString(api.metadata(mod, "title"));
    // XM/IT name instruments; MOD/S3M only have samples (whose names are
    // where scene greetings traditionally live) — show whichever exists.
    if (api.numInstruments && api.numSamples) {
        int ni = api.numInstruments(mod);
        if (ni > 0 && api.instrumentName) {
            for (int i = 0; i < ni; ++i)
                g_tkInstr.push_back(mptString(api.instrumentName(mod, i)));
        } else if (api.numSamples(mod) > 0 && api.sampleName) {
            for (int i = 0, n = api.numSamples(mod); i < n; ++i)
                g_tkInstr.push_back(mptString(api.sampleName(mod, i)));
        }
    }
}

// Append a timeline event when the playing row changes.
void recordRow(void* mod, size_t frame) {
    const MptApi& api = mpt();
    if (!api.okInfo || !g_tkChannels) return;
    int order = api.curOrder(mod), row = api.curRow(mod);
    if (!g_tkTimeline.empty() && g_tkTimeline.back().order == order &&
        g_tkTimeline.back().row == row)
        return;
    int pat = order < (int)g_tkOrderPat.size() ? g_tkOrderPat[order] : -1;
    g_tkTimeline.push_back({(uint32_t)frame, (int16_t)order, (int16_t)pat,
                            (int16_t)row, (int16_t)api.curSpeed(mod),
                            (int16_t)api.curTempo(mod)});
}

// Render each channel solo (all others muted) at kScopeRate mono, stored as
// int8 — the per-channel oscilloscope signal. N extra passes over the song,
// but at a quarter of the playback rate and one mixed channel each.
void renderScopes(const std::vector<uint8_t>& file, double seconds) {
    const MptApi& api = mpt();
    if (!api.okExt || g_tkChannels <= 0) return;
    void* ext = api.extCreate(file.data(), file.size(), nullptr, nullptr,
                              nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!ext) return;
    void* mod = api.extGetModule(ext);
    MptInteractive ia{};
    if (!mod ||
        !api.extGetInterface(ext, "interactive", &ia, sizeof ia) ||
        !ia.setChannelMute) {
        api.extDestroy(ext);
        return;
    }
    api.setRepeat(mod, 0);

    const size_t cap = (size_t)(seconds * kScopeRate) + kScopeRate / 4;
    g_tkScope.assign(g_tkChannels, {});
    float buf[4096];
    for (int ch = 0; ch < g_tkChannels; ++ch) {
        for (int c = 0; c < g_tkChannels; ++c)
            ia.setChannelMute(ext, c, c != ch);
        api.setPosSeconds(mod, 0);
        std::vector<int8_t>& dst = g_tkScope[ch];
        dst.reserve(cap);
        while (dst.size() < cap) {
            size_t n = api.readMono(mod, kScopeRate,
                                    std::min<size_t>(4096, cap - dst.size()),
                                    buf);
            if (n == 0) break;
            for (size_t i = 0; i < n; ++i)
                dst.push_back((int8_t)std::clamp(buf[i] * 127.0f,
                                                 -127.0f, 127.0f));
        }
    }
    api.extDestroy(ext);
}

// PCM frames the device has actually played (render-rate frames).
uint32_t playedFrames() {
    if (!g_pcm) return 0;
    const int fb = g_spec.channels * (SDL_AUDIO_BITSIZE(g_spec.format) / 8);
    if (fb <= 0) return 0;
    Uint32 queued = g_stream ? (Uint32)SDL_GetAudioStreamQueued(g_stream) : 0;
    Uint32 played = g_cursor > queued ? g_cursor - queued : 0;
    return played / (Uint32)fb;
}

bool renderOpenmpt(const std::vector<uint8_t>& file, int rate,
                   std::vector<uint8_t>& pcm) {
    const MptApi& api = mpt();
    if (!api.ok) return false;
    void* mod = api.create(file.data(), file.size(), nullptr, nullptr,
                           nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!mod) return false;
    api.setRepeat(mod, 0);  // one pass; a 0-frame read marks the song end

    captureModuleInfo(mod);

    // 1024-frame chunks: ~21ms of row-timeline resolution, well under one
    // row even at fast tempos.
    float buf[1024 * 2];
    size_t frames = 0;
    const size_t capFrames = (size_t)rate * kCapSeconds;
    while (frames < capFrames) {
        recordRow(mod, frames);
        size_t n = api.readF32(mod, rate, 1024, buf);
        if (n == 0) break;
        const uint8_t* p = (const uint8_t*)buf;
        pcm.insert(pcm.end(), p, p + n * 2 * sizeof(float));
        frames += n;
    }
    api.destroy(mod);
    if (pcm.empty()) return false;

    renderScopes(file, (double)frames / rate);
    return true;
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
    clearTrackerData();
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
    bool viaOpenmpt = renderOpenmpt(file, rate, pcm);
    if (!viaOpenmpt) clearTrackerData();  // drop partial capture
    if (!viaOpenmpt && !renderPocketmod(file, rate, pcm)) {
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
    clearTrackerData();
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

// ------------------------------------------------------------ tracker data

int trackerChannels() { return g_pcm ? g_tkChannels : 0; }
int trackerOrders()   { return (int)g_tkOrderPat.size(); }

int trackerOrderPattern(int order) {
    return order >= 0 && order < (int)g_tkOrderPat.size() ? g_tkOrderPat[order]
                                                          : -1;
}

int trackerPatternRows(int pattern) {
    return pattern >= 0 && pattern < (int)g_tkPatRows.size()
               ? g_tkPatRows[pattern] : 0;
}

const std::string& trackerCell(int pattern, int row, int channel) {
    static const std::string kEmpty;
    if (pattern < 0 || pattern >= (int)g_tkCells.size()) return kEmpty;
    if (row < 0 || channel < 0 || channel >= g_tkChannels) return kEmpty;
    size_t i = (size_t)row * g_tkChannels + channel;
    return i < g_tkCells[pattern].size() ? g_tkCells[pattern][i] : kEmpty;
}

const std::string& trackerTitle() { return g_tkTitle; }
const std::vector<std::string>& trackerInstruments() { return g_tkInstr; }

bool trackerState(TrackerState& out) {
    if (!g_pcm || !g_tkChannels || g_tkTimeline.empty()) return false;
    uint32_t frame = playedFrames();
    // last event that starts at or before the playhead
    size_t lo = 0, hi = g_tkTimeline.size();
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (g_tkTimeline[mid].frame <= frame) lo = mid;
        else hi = mid;
    }
    const RowEvent& e = g_tkTimeline[lo];
    out.order   = e.order;
    out.pattern = e.pattern;
    out.row     = e.row;
    out.numRows = trackerPatternRows(e.pattern);
    out.speed   = e.speed;
    out.tempo   = e.tempo;
    return true;
}

int trackerScopeRate() { return kScopeRate; }

int trackerScope(int channel, float* out, int n) {
    if (!g_pcm || channel < 0 || channel >= (int)g_tkScope.size()) return 0;
    const std::vector<int8_t>& src = g_tkScope[channel];
    size_t pos = (size_t)((double)playedFrames() / g_spec.freq * kScopeRate);
    int got = 0;
    for (; got < n && pos < src.size(); ++got, ++pos)
        out[got] = src[pos] / 127.0f;
    return got;
}

void setVolume(float v) {
    g_volume = std::clamp(v, 0.0f, 1.0f);
    if (g_stream) SDL_SetAudioStreamGain(g_stream, g_volume);
}

float volume() { return g_volume; }

void update() { topUp(); }

}  // namespace playback
