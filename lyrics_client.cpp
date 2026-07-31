#include "lyrics_client.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace {

String urlEncode(const String &s) {
  String encoded;
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

// Parses a single LRC line like "[01:23.45]Some lyric text".
// Returns false for lines with no valid timestamp tag (e.g. metadata lines like "[ar:Artist]").
bool parseLrcLine(const String &line, long &outMs, String &outText) {
  if (line.length() < 2 || line[0] != '[') return false;
  int close = line.indexOf(']');
  if (close < 0) return false;

  String tag = line.substring(1, close);
  int colon = tag.indexOf(':');
  if (colon < 0) return false;

  for (size_t i = 0; i < tag.length(); i++) {
    if (!isdigit((unsigned char)tag[i]) && tag[i] != ':' && tag[i] != '.') return false;
  }

  int minutes = tag.substring(0, colon).toInt();
  float seconds = tag.substring(colon + 1).toFloat();
  outMs = (long)(minutes * 60000L + seconds * 1000.0f);
  outText = line.substring(close + 1);
  outText.trim();
  return true;
}

}  // namespace

bool LyricsClient::fetchSynced(const String &trackName, const String &artistName, long durationSec,
                                LyricLine *outLines, int maxLines, int &outCount) {
  outCount = 0;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://lrclib.net/api/get?track_name=" + urlEncode(trackName) +
               "&artist_name=" + urlEncode(artistName);
  if (durationSec > 0) {
    url += "&duration=" + String(durationSec);
  }

  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Lyrics] fetch failed, HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Lyrics] Failed to parse response (%d bytes): %s\n", payload.length(), err.c_str());
    return false;
  }

  const char *synced = doc["syncedLyrics"];
  if (!synced) {
    Serial.println("[Lyrics] No synced lyrics available for this track");
    return false;
  }

  String all(synced);
  int start = 0;
  while (start < (int)all.length() && outCount < maxLines) {
    int nl = all.indexOf('\n', start);
    String line = (nl < 0) ? all.substring(start) : all.substring(start, nl);

    long ms;
    String text;
    if (parseLrcLine(line, ms, text)) {
      outLines[outCount].timeMs = ms;
      outLines[outCount].text = text;
      outCount++;
    }

    if (nl < 0) break;
    start = nl + 1;
  }

  return outCount > 0;
}
