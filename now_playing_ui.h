#pragma once
#include "spotify_client.h"

// Call once, AFTER lvgl and the display are already initialized by the Waveshare example.
// Builds the labels/art/buttons on the current LVGL screen.
void nowPlayingUI_init();

// Call whenever you have a fresh NowPlaying struct (e.g. every few seconds).
// Only re-downloads/re-decodes the album art if the URL actually changed.
void nowPlayingUI_update(const NowPlaying &np);

// Call from loop() (not from an LVGL callback) to execute any pending playback
// action (play/pause/next/previous/shuffle/repeat) queued by a button press. This makes a
// blocking network call, so it must run off the LVGL task - the button
// callbacks just queue the request instead of calling SpotifyClient directly,
// so taps stay visually responsive instead of freezing the UI.
// Returns true if an action was executed, so loop() can force an immediate
// Spotify poll to reflect the change without waiting for the next scheduled one.
bool nowPlayingUI_processPendingAction();
