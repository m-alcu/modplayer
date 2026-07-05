cmake_minimum_required(VERSION 3.16)
project(modplayer C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# iPod-nano-styled ProTracker MOD player (SDL3 + pocketmod + stb_image,
# vendored alongside the sources in src/).
add_executable(modplayer
    src/main.cpp
    src/core.cpp
    src/skin_nano.cpp
    src/skin_classic.cpp
    src/skin_winamp.cpp
    src/skin_foobar.cpp
    src/skin_zune.cpp
    src/skin_cassette.cpp
    src/skin_vinyl.cpp
    src/skin_car.cpp
    src/skin_term.cpp
    src/skin_mini.cpp
    src/library.cpp
    src/tags.cpp
    src/playback.cpp
    src/pocketmod_impl.c
)
target_link_libraries(modplayer PRIVATE ${CMAKE_DL_LIBS})

# Absolute path to the repo so the default library can fall back to
# assets/sounds/ regardless of the working directory it is launched from.
target_compile_definitions(modplayer PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")

# Link SDL3. Prefer pkg-config (reliable on Linux); fall back to the SDL3 CMake
# config package (common on macOS/vcpkg/Windows).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
endif()

if(TARGET PkgConfig::SDL3)
    target_link_libraries(modplayer PRIVATE PkgConfig::SDL3)
else()
    find_package(SDL3 REQUIRED CONFIG)
    target_link_libraries(modplayer PRIVATE SDL3::SDL3)
endif()

if(NOT MSVC)
    target_compile_options(modplayer PRIVATE -Wall -Wextra)
endif()
