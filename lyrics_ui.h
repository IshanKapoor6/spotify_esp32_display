#pragma once
#include "spotify_client.h"

// Call once during setup, after nowPlayingUI_init() (must hold the LVGL mutex).
void lyricsUI_init();

// Call from the "Lyrics" button's LVGL click callback. Just flags a request -
// the actual (blocking, network) fetch happens later from lyricsUI_onNowPlaying(),
// off the LVGL task, so touch/rendering doesn't stall while we wait on HTTP.
void lyricsUI_requestOpen();

// Call every time you have a fresh NowPlaying (e.g. every poll cycle).
// Services a pending open request, and refetches lyrics if the track changed
// while the lyrics screen is already showing.
void lyricsUI_onNowPlaying(const NowPlaying &np);

// Call frequently (e.g. every ~250ms) to keep the highlighted/scrolled line in
// sync with playback, interpolated between polls. No-op if the lyrics screen
// isn't showing.
void lyricsUI_tick();

bool lyricsUI_isActive();
