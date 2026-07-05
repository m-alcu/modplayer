#include "library.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <set>

namespace {

bool hasAudioExt(const std::string& name) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".mod" || ext == ".xm" || ext == ".s3m" || ext == ".it";
}

// Case-insensitive compare that ignores a leading "The " (so "The Beatles"
// files under B, like the iPod does).
bool lessName(const std::string& a, const std::string& b) {
    auto key = [](const std::string& s) {
        std::string k = s;
        if (k.size() > 4 && (k.compare(0, 4, "The ") == 0 ||
                             k.compare(0, 4, "the ") == 0 ||
                             k.compare(0, 4, "THE ") == 0))
            k.erase(0, 4);
        for (char& c : k) c = (char)std::tolower((unsigned char)c);
        return k;
    };
    std::string ka = key(a), kb = key(b);
    if (ka != kb) return ka < kb;
    return a < b;
}

struct ScanCtx {
    std::vector<std::string> files;
    int depth = 0;
};

SDL_EnumerationResult SDLCALL onEntry(void* ud, const char* dir, const char* name) {
    ScanCtx* ctx = (ScanCtx*)ud;
    if (name[0] == '.') return SDL_ENUM_CONTINUE;  // hidden files / . / ..
    std::string full = std::string(dir);
    if (!full.empty() && full.back() != '/') full += '/';
    full += name;

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(full.c_str(), &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (ctx->depth < 12) {
            ++ctx->depth;
            SDL_EnumerateDirectory(full.c_str(), onEntry, ud);
            --ctx->depth;
        }
    } else if (hasAudioExt(name)) {
        ctx->files.push_back(full);
    }
    return SDL_ENUM_CONTINUE;
}

std::string fileStem(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name.resize(dot);
    return name;
}

}  // namespace

const char* Library::kUnknownArtist = "Unknown Artist";
const char* Library::kUnknownAlbum  = "Unknown Album";

std::string Library::artistOf(int idx) const {
    const std::string& a = tracks_[idx].meta.artist;
    return a.empty() ? kUnknownArtist : a;
}
std::string Library::albumOf(int idx) const {
    const std::string& a = tracks_[idx].meta.album;
    return a.empty() ? kUnknownAlbum : a;
}

int Library::scan(const std::string& dir) {
    tracks_.clear();
    root_ = dir;

    ScanCtx ctx;
    SDL_EnumerateDirectory(dir.c_str(), onEntry, &ctx);
    std::sort(ctx.files.begin(), ctx.files.end());

    for (const std::string& f : ctx.files) {
        Track t;
        t.path = f;
        tags::read(f, t.meta);
        t.title = t.meta.title.empty() ? fileStem(f) : t.meta.title;
        tracks_.push_back(std::move(t));
    }
    return (int)tracks_.size();
}

std::vector<std::string> Library::artists() const {
    std::set<std::string> seen;
    for (int i = 0; i < (int)tracks_.size(); ++i) seen.insert(artistOf(i));
    std::vector<std::string> v(seen.begin(), seen.end());
    std::sort(v.begin(), v.end(), lessName);
    return v;
}

std::vector<std::string> Library::albums() const {
    std::set<std::string> seen;
    for (int i = 0; i < (int)tracks_.size(); ++i) seen.insert(albumOf(i));
    std::vector<std::string> v(seen.begin(), seen.end());
    std::sort(v.begin(), v.end(), lessName);
    return v;
}

std::vector<std::string> Library::albumsOf(const std::string& artist) const {
    std::set<std::string> seen;
    for (int i = 0; i < (int)tracks_.size(); ++i)
        if (artistOf(i) == artist) seen.insert(albumOf(i));
    std::vector<std::string> v(seen.begin(), seen.end());
    std::sort(v.begin(), v.end(), lessName);
    return v;
}

std::vector<int> Library::years() const {
    std::set<int> seen;
    for (const Track& t : tracks_) seen.insert(t.meta.year);
    return std::vector<int>(seen.begin(), seen.end());
}

std::vector<int> Library::songsByTitle() const {
    std::vector<int> v(tracks_.size());
    for (int i = 0; i < (int)v.size(); ++i) v[i] = i;
    std::sort(v.begin(), v.end(), [&](int a, int b) {
        return lessName(tracks_[a].title, tracks_[b].title);
    });
    return v;
}

std::vector<int> Library::tracksOfArtist(const std::string& artist) const {
    std::vector<int> v;
    for (int i = 0; i < (int)tracks_.size(); ++i)
        if (artistOf(i) == artist) v.push_back(i);
    // Album order, then track number, then title — a sensible "All songs" order.
    std::sort(v.begin(), v.end(), [&](int a, int b) {
        if (albumOf(a) != albumOf(b)) return lessName(albumOf(a), albumOf(b));
        if (tracks_[a].meta.track != tracks_[b].meta.track)
            return tracks_[a].meta.track < tracks_[b].meta.track;
        return lessName(tracks_[a].title, tracks_[b].title);
    });
    return v;
}

std::vector<int> Library::tracksOfAlbum(const std::string& album,
                                        const std::string& artist) const {
    std::vector<int> v;
    for (int i = 0; i < (int)tracks_.size(); ++i)
        if (albumOf(i) == album && (artist.empty() || artistOf(i) == artist))
            v.push_back(i);
    std::sort(v.begin(), v.end(), [&](int a, int b) {
        if (tracks_[a].meta.track != tracks_[b].meta.track)
            return tracks_[a].meta.track < tracks_[b].meta.track;
        return lessName(tracks_[a].title, tracks_[b].title);
    });
    return v;
}

std::vector<int> Library::tracksOfYear(int year) const {
    std::vector<int> v;
    for (int i = 0; i < (int)tracks_.size(); ++i)
        if (tracks_[i].meta.year == year) v.push_back(i);
    std::sort(v.begin(), v.end(), [&](int a, int b) {
        if (artistOf(a) != artistOf(b)) return lessName(artistOf(a), artistOf(b));
        if (albumOf(a) != albumOf(b)) return lessName(albumOf(a), albumOf(b));
        if (tracks_[a].meta.track != tracks_[b].meta.track)
            return tracks_[a].meta.track < tracks_[b].meta.track;
        return lessName(tracks_[a].title, tracks_[b].title);
    });
    return v;
}
