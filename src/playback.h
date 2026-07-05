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

// Per-frame housekeeping: keeps the audio stream topped up. Call every frame.
void update();

}  // namespace playback
