#pragma once
#include <Arduino.h>

struct NowPlaying {
  bool isPlaying = false;
  bool hasTrack = false;
  String trackName;
  String artistName;
  String albumArtUrl;   // largest available album art URL
  long progressMs = 0;
  long durationMs = 0;
};

// A row in the playlists list. Fixed-size arrays of these (rather than a
// dynamic container) keep memory use predictable on the ESP32.
struct PlaylistBrief {
  String id;
  String name;
  int trackCount = 0;
};

// A row in a track list (a playlist's tracks, or Liked Songs).
struct TrackBrief {
  String uri;
  String name;
  String artistName;
};

#define SPOTIFY_MAX_PLAYLISTS 50
#define SPOTIFY_MAX_TRACKS 50

class SpotifyClient {
 public:
  // Call once after WiFi connects. Exchanges the refresh token for a first access token.
  bool begin();

  // Call periodically (e.g. every 3-5s). Refreshes the access token automatically when needed.
  bool getNowPlaying(NowPlaying &out);

  bool play();
  bool pause();
  bool next();
  bool previous();

  // Library browsing. Needs the refresh token to be scoped for
  // playlist-read-private and user-library-read - see README.
  // Note: there's no getPlaylistTracks() - as of Spotify's late-2024 API
  // changes, "Get Playlist Items" 403s for apps without an extended-access
  // grant (which Spotify only issues to reviewed/approved apps, not hobby
  // projects). Liked Songs isn't affected, so that one still works fully.
  bool getPlaylists(PlaylistBrief *out, int maxCount, int &outCount);
  bool getLikedSongs(TrackBrief *out, int maxCount, int &outCount);

  // Starts playback of a playlist from the top (no per-track offset, since
  // we can't read a playlist's track list - see the note above).
  bool playPlaylist(const String &playlistId);
  // Starts playback of an explicit list of track URIs and continues through
  // them in order afterward. Used for Liked Songs, which has no context_uri.
  bool playTrackUris(const String *uris, int count);

 private:
  bool refreshAccessToken();

  String _accessToken;
  unsigned long _accessTokenExpiresAt = 0;  // millis() timestamp
};
