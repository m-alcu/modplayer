#pragma once

// ---------------------------------------------------------------------------
// Metadata reader for tracker modules: .mod / .xm / .s3m / .it.
//
// Each format keeps a song name at a fixed offset in its header (→ title),
// and the format + channel count becomes the "album" ("4-channel MOD",
// "16-channel XM"…), so the Albums view groups modules by format. Modules
// know nothing of artists, years or embedded art; those stay empty and
// cover art comes from folder images alone.
//
// All text is returned as UTF-8 (module names are Latin-1-ish bytes).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace tags {

struct Meta {
    std::string title;    // empty when the file carries no tag
    std::string artist;
    std::string album;
    int         year   = 0;  // 0 = unknown
    int         track  = 0;  // track number within the album, 0 = unknown
    bool        hasArt = false;
};

// Read the text metadata of a module file (cheap: reads only the header).
// Missing fields are left empty/0. Returns false only when the file can't
// be opened.
bool read(const std::string& path, Meta& out);

// Embedded cover art — modules have none, so this always returns an empty
// vector. Kept so the UI's art lookup (embedded → folder image) reads the
// same as in any player.
std::vector<uint8_t> readArtRGBA(const std::string& path, int& w, int& h);

// Decode an image file (PNG/JPEG/BMP) to RGBA32 — used for folder art like
// cover.jpg. Empty vector on failure.
std::vector<uint8_t> loadImageRGBA(const std::string& path, int& w, int& h);

}  // namespace tags
