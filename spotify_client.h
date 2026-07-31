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

 private:
  bool refreshAccessToken();

  String _accessToken;
  unsigned long _accessTokenExpiresAt = 0;  // millis() timestamp
};
