#include "tags.h"

#include <SDL3/SDL.h>  // must precede stb_image.h (provides Uint8/Uint16)

#include <cmath>
#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#include "stb_image.h"

namespace tags {
namespace {

// Song and sample names in a MOD are NUL-padded fixed-width fields in the
// Amiga charset (close enough to Latin-1). Trim padding/whitespace and fold
// high bytes to UTF-8 so the rest of the player only ever sees UTF-8.
std::string modText(const uint8_t* p, size_t n) {
    while (n && (p[n - 1] == 0 || p[n - 1] == ' ')) --n;
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c == 0) break;
        if (c < 0x20) c = ' ';  // stray control bytes
        if (c < 0x80) {
            out += (char)c;
        } else {  // Latin-1 → UTF-8
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// The 4-byte magic at offset 1080 identifies a 31-sample MOD and its channel
// count. Returns 0 when the magic is unknown (15-sample originals and
// non-MOD files alike).
int modChannels(const uint8_t m[4]) {
    if (!std::memcmp(m, "M.K.", 4) || !std::memcmp(m, "M!K!", 4) ||
        !std::memcmp(m, "FLT4", 4))
        return 4;
    if (m[0] >= '1' && m[0] <= '9' && !std::memcmp(m + 1, "CHN", 3))
        return m[0] - '0';
    if (m[0] >= '1' && m[0] <= '9' && m[1] >= '0' && m[1] <= '9' &&
        !std::memcmp(m + 2, "CH", 2))
        return (m[0] - '0') * 10 + (m[1] - '0');
    return 0;
}

std::vector<uint8_t> decodeRGBA(const std::vector<uint8_t>& raw, int& w,
                                int& h) {
    if (raw.empty()) return {};
    int comp = 0;
    stbi_uc* px = stbi_load_from_memory(raw.data(), (int)raw.size(),
                                        &w, &h, &comp, 4);
    if (!px) { w = h = 0; return {}; }
    std::vector<uint8_t> rgba(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return rgba;
}

}  // namespace

bool read(const std::string& path, Meta& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out = Meta{};

    // Every supported header fits in the first 1084 bytes: the song name and
    // format magic live at fixed offsets in each tracker format.
    uint8_t hdr[1084];
    size_t got = std::fread(hdr, 1, sizeof hdr, f);
    std::fclose(f);

    if (got >= 30 && !std::memcmp(hdr, "IMPM", 4)) {
        // Impulse Tracker: 26-byte song name at offset 4.
        out.title = modText(hdr + 4, 26);
        out.album = "Impulse Tracker IT";
    } else if (got >= 80 && !std::memcmp(hdr, "Extended Module: ", 17)) {
        // FastTracker II: 20-byte name at 17, channel count (LE16) at 68.
        out.title = modText(hdr + 17, 20);
        int ch = hdr[68] | (hdr[69] << 8);
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d-channel XM", ch);
        out.album = buf;
    } else if (got >= 96 && !std::memcmp(hdr + 44, "SCRM", 4)) {
        // Scream Tracker 3: 28-byte name at 0; 32 channel-setting bytes at
        // 64 where a value with bit 7 clear is an enabled channel.
        out.title = modText(hdr, 28);
        int ch = 0;
        for (int i = 0; i < 32; ++i) ch += hdr[64 + i] < 128;
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d-channel S3M", ch);
        out.album = buf;
    } else if (got >= sizeof hdr) {
        // ProTracker: 20-byte song name at 0, format magic at 1080.
        out.title = modText(hdr, 20);
        if (int ch = modChannels(hdr + 1080)) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%d-channel MOD", ch);
            out.album = buf;
        }
    }
    return true;
}

std::vector<uint8_t> readArtRGBA(const std::string&, int& w, int& h) {
    w = h = 0;
    return {};  // MODs carry no embedded picture; folder art still applies
}

std::vector<uint8_t> loadImageRGBA(const std::string& path, int& w, int& h) {
    w = h = 0;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::rewind(f);
    std::vector<uint8_t> raw;
    if (n > 0 && n < 64 * 1024 * 1024) {
        raw.resize((size_t)n);
        if (std::fread(raw.data(), 1, raw.size(), f) != raw.size())
            raw.clear();
    }
    std::fclose(f);
    return decodeRGBA(raw, w, h);
}

}  // namespace tags
