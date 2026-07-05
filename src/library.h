#pragma once

// ---------------------------------------------------------------------------
// Music library: a recursive scan of one folder for tracker modules
// (.mod/.xm/.s3m/.it) plus the grouped views the menus browse — Artists,
// Albums, Songs, Years.
//
// Modules without a song name still show up: the title falls back to the
// file name, and fields MODs don't carry group under "Unknown".
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "tags.h"

struct Track {
    std::string path;
    tags::Meta  meta;
    std::string title;  // meta.title, or the file name when untagged
};

class Library {
public:
    // Scan `dir` recursively. Returns the number of tracks found.
    int scan(const std::string& dir);

    const std::vector<Track>& tracks() const { return tracks_; }
    const std::string& root() const { return root_; }

    // Grouped views. Group labels are display-ready ("Unknown Artist"…);
    // track lists hold indexes into tracks().
    std::vector<std::string> artists() const;
    std::vector<std::string> albums() const;                       // all albums
    std::vector<std::string> albumsOf(const std::string& artist) const;
    std::vector<int>         years() const;

    std::vector<int> songsByTitle() const;                         // every track
    std::vector<int> tracksOfArtist(const std::string& artist) const;
    std::vector<int> tracksOfAlbum(const std::string& album,
                                   const std::string& artist = "") const;
    std::vector<int> tracksOfYear(int year) const;

    // Display fallbacks applied to a track's empty fields.
    static const char* kUnknownArtist;
    static const char* kUnknownAlbum;

    std::string artistOf(int idx) const;
    std::string albumOf(int idx) const;

private:
    std::vector<Track> tracks_;
    std::string        root_;
};
