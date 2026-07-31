#pragma once
#include <Arduino.h>

#define LYRICS_MAX_LINES 400

struct LyricLine {
  long timeMs;
  String text;
};

class LyricsClient {
 public:
  // Fetches time-synced lyrics for a track from lrclib.net.
  // durationSec may be 0 if unknown - the match will just be less precise.
  // Returns true and fills outLines/outCount if synced lyrics were found.
  bool fetchSynced(const String &trackName, const String &artistName, long durationSec,
                    LyricLine *outLines, int maxLines, int &outCount);
};
