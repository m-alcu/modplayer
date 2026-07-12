#pragma once

// ---------------------------------------------------------------------------
// Playback engine. A tracker module (MOD/XM/S3M/IT) is rendered whole to
// PCM at load — one full pass of the song via the system libopenmpt when
// available, else the vendored pocketmod (ProTracker MODs only) — and
// drip-fed into an SDL_AudioStream bound to the default output device.
// Holding the full PCM makes position tracking and seeking trivial.
//
// If no audio device is available every call is a safe no-op, so the UI can
// still browse the library (and headless tests can run).
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

namespace playback {

bool init();      // open the default output device; false → engine muted
void shutdown();
bool ready();     // true when an output device was opened

// Decode `path` and start playing it from the beginning.
// Returns false if the file can't be decoded.
bool play(const std::string& path);
void stop();      // drop the current track

void   setPaused(bool p);
bool   paused();
bool   active();          // a track is loaded (playing or paused)
bool   finished();        // current track fully drained (poll after update())

double position();        // seconds into the track
double duration();        // track length in seconds (0 when nothing loaded)
void   seekTo(double secs);

void  setVolume(float v); // 0..1
float volume();

int channels();           // of the loaded track (0 when nothing loaded)
int sampleRate();

// Copy up to `n` mono samples starting at the playhead into `out` (for
// visualizers). Returns the number written (0 when idle or format unsupported).
int visSamples(float* out, int n);

// ------------------------------------------------------------ tracker data
// Pattern/channel introspection for tracker-style skins, captured at load
// time when the module was decoded via libopenmpt (empty/zero on the
// pocketmod fallback). Playhead-dependent queries follow the PCM position,
// so they stay correct across pause and seek.

struct TrackerState {
    int order, pattern, row;  // playhead position in the module
    int numRows;              // rows in that pattern
    int speed, tempo;         // ticks/row and BPM at the playhead
};

int  trackerChannels();                  // 0 → no tracker data for this track
bool trackerState(TrackerState& out);    // false when no data
int  trackerOrders();                    // length of the order list
int  trackerOrderPattern(int order);     // pattern played at `order` (-1 bad)
int  trackerPatternRows(int pattern);    // rows in `pattern` (0 bad)

// Formatted pattern cell, always 14 chars: "C-5 01 v64 A0F" style
// (note, instrument, volume, effect). Empty string out of range.
const std::string& trackerCell(int pattern, int row, int channel);

const std::string& trackerTitle();       // module title ("" when unknown)
const std::vector<std::string>& trackerInstruments();  // instrument/sample names

// Mono waveform of one channel: up to `n` samples starting at the playhead,
// at trackerScopeRate() Hz. Returns samples written (0 → no scope data).
int trackerScope(int channel, float* out, int n);
int trackerScopeRate();

// Per-frame housekeeping: keeps the audio stream topped up. Call every frame.
void update();

}  // namespace playback
