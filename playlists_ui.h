#pragma once
#include "spotify_client.h"

// Call once during setup, after nowPlayingUI_init() (must hold the LVGL mutex).
void playlistsUI_init();

// Call from the "Playlists" button's LVGL click callback on the now-playing
// screen. Just flags a request - the actual (blocking) fetch happens in
// playlistsUI_tick(), off the LVGL task, so touch/rendering doesn't stall.
void playlistsUI_requestOpen();

// Call from loop() (not from an LVGL callback). Services any pending
// open-library / open-Liked-Songs / play-playlist / play-track request
// queued by touch. Makes blocking network calls, so it must run off the
// LVGL task.
void playlistsUI_tick();

bool playlistsUI_isActive();
