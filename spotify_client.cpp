#include "spotify_client.h"
#include "secrets.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>  // built into arduino-esp32, for Base64::encode

bool SpotifyClient::begin() {
  return refreshAccessToken();
}

bool SpotifyClient::refreshAccessToken() {
  WiFiClientSecure client;
  client.setInsecure();  // simplest path for a hobby project; see README for hardening notes

  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String creds = String(SPOTIFY_CLIENT_ID) + ":" + String(SPOTIFY_CLIENT_SECRET);
  String basicAuth = "Basic " + base64::encode(creds);
  http.addHeader("Authorization", basicAuth);

  String body = String("grant_type=refresh_token&refresh_token=") + SPOTIFY_REFRESH_TOKEN;

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[Spotify] Token refresh failed, HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Spotify] Failed to parse token response: %s\n", err.c_str());
    return false;
  }

  _accessToken = doc["access_token"].as<String>();
  long expiresIn = doc["expires_in"] | 3600;
  _accessTokenExpiresAt = millis() + (expiresIn - 60) * 1000UL;  // refresh 60s early

  Serial.println("[Spotify] Access token refreshed");
  return true;
}

bool SpotifyClient::getNowPlaying(NowPlaying &out) {
  if (millis() > _accessTokenExpiresAt) {
    if (!refreshAccessToken()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  // /v1/me/player (not /v1/me/player/currently-playing) - the currently-playing
  // endpoint doesn't include shuffle_state/repeat_state, which we need for the
  // shuffle/repeat buttons to reflect the real player state.
  http.begin(client, "https://api.spotify.com/v1/me/player");
  http.addHeader("Authorization", "Bearer " + _accessToken);

  int code = http.GET();

  if (code == 204) {
    // Nothing currently playing
    out = NowPlaying();
    http.end();
    return true;
  }

  if (code != 200) {
    Serial.printf("[Spotify] currently-playing HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  // Elastic JsonDocument (not a fixed-capacity DynamicJsonDocument): this response
  // can be much bigger than it looks, since Spotify includes an `available_markets`
  // array (100+ country codes) on both the track and album objects by default. A
  // fixed 6KB buffer silently failed to parse on some tracks, which left the UI
  // stuck on stale/"Nothing playing" data forever since this returned false.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Spotify] Failed to parse now-playing response (%d bytes): %s\n",
                  payload.length(), err.c_str());
    return false;
  }

  out = NowPlaying();
  out.isPlaying = doc["is_playing"] | false;
  out.progressMs = doc["progress_ms"] | 0;
  out.shuffleState = doc["shuffle_state"] | false;
  out.repeatState = doc["repeat_state"] | "off";

  JsonObject item = doc["item"];
  if (item.isNull()) {
    return true;  // e.g. an ad or unsupported content, still a valid "nothing to show" state
  }

  out.hasTrack = true;
  out.trackName = item["name"].as<String>();
  out.durationMs = item["duration_ms"] | 0;

  JsonArray artists = item["artists"];
  if (artists.size() > 0) {
    out.artistName = artists[0]["name"].as<String>();
  }

  // Spotify normally returns 3 sizes: [0]=~640x640, [1]=~300x300, [2]=~64x64.
  // We want the middle one - big enough to look good, small enough to decode
  // comfortably in RAM on the ESP32.
  JsonArray images = item["album"]["images"];
  if (images.size() >= 2) {
    out.albumArtUrl = images[1]["url"].as<String>();
  } else if (images.size() > 0) {
    out.albumArtUrl = images[0]["url"].as<String>();
  }

  return true;
}

bool SpotifyClient::play() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/play");
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Length", "0");
  int code = http.PUT("");
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] play failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] play OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::pause() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/pause");
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Length", "0");
  int code = http.PUT("");
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] pause failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] pause OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::next() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/next");
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Length", "0");
  int code = http.POST("");
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] next failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] next OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::previous() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/previous");
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Length", "0");
  int code = http.POST("");
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] previous failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] previous OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::setShuffle(bool enable) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.spotify.com/v1/me/player/shuffle?state=") + (enable ? "true" : "false");
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Length", "0");
  int code = http.PUT("");
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] set shuffle failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] set shuffle OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::setRepeatMode(const String &mode) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.spotify.com/v1/me/player/repeat?state=" + mode;
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Length", "0");
  int code = http.PUT("");
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] set repeat failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] set repeat OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::getPlaylists(PlaylistBrief *out, int maxCount, int &outCount) {
  outCount = 0;
  if (millis() > _accessTokenExpiresAt) {
    if (!refreshAccessToken()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // Trimmed down with `fields` - unfiltered this is a *full* playlist object per
  // item (description, images, full owner object, etc.), which for 50 playlists
  // was ~58KB and exhausted internal heap badly enough to abort the whole board
  // (WiFi's PHY driver couldn't even allocate an internal timer). Filtered it's ~3KB.
  String url = "https://api.spotify.com/v1/me/playlists?limit=" + String(maxCount) +
               "&fields=items(id,name,tracks.total)";
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + _accessToken);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Spotify] get playlists HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Spotify] Failed to parse playlists response (%d bytes): %s\n",
                  payload.length(), err.c_str());
    return false;
  }

  for (JsonObject item : doc["items"].as<JsonArray>()) {
    if (outCount >= maxCount || item.isNull()) continue;
    out[outCount].id = item["id"].as<String>();
    out[outCount].name = item["name"].as<String>();
    out[outCount].trackCount = item["tracks"]["total"] | 0;
    outCount++;
  }

  return true;
}

bool SpotifyClient::getLikedSongs(TrackBrief *out, int maxCount, int &outCount) {
  outCount = 0;
  if (millis() > _accessTokenExpiresAt) {
    if (!refreshAccessToken()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  // Trimmed down with `fields`, same as the old playlist-tracks call - without
  // it this response is 90KB+ (full track/album objects with market lists),
  // which was blowing out the ESP32's heap and crashing mid-parse.
  String url = "https://api.spotify.com/v1/me/tracks?limit=" + String(maxCount) +
               "&fields=items(track(uri,name,artists(name)))";
  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + _accessToken);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Spotify] get liked songs HTTP %d: %s\n", code, http.getString().c_str());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[Spotify] Failed to parse liked songs response (%d bytes): %s\n",
                  payload.length(), err.c_str());
    return false;
  }

  for (JsonObject item : doc["items"].as<JsonArray>()) {
    if (outCount >= maxCount) continue;
    JsonObject track = item["track"];
    if (track.isNull()) continue;  // e.g. a local file with no track object
    String uri = track["uri"] | "";
    if (uri.length() == 0) continue;  // local files aren't playable via the API
    out[outCount].uri = uri;
    out[outCount].name = track["name"].as<String>();
    JsonArray artists = track["artists"];
    if (artists.size() > 0) {
      out[outCount].artistName = artists[0]["name"].as<String>();
    }
    outCount++;
  }

  return true;
}

bool SpotifyClient::playPlaylist(const String &playlistId) {
  if (millis() > _accessTokenExpiresAt) {
    if (!refreshAccessToken()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/play");
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Type", "application/json");

  JsonDocument body;
  body["context_uri"] = "spotify:playlist:" + playlistId;
  String json;
  serializeJson(body, json);

  int code = http.PUT(json);
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] play playlist failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] play playlist OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}

bool SpotifyClient::playTrackUris(const String *uris, int count) {
  if (count <= 0) return false;
  if (millis() > _accessTokenExpiresAt) {
    if (!refreshAccessToken()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/play");
  http.addHeader("Authorization", "Bearer " + _accessToken);
  http.addHeader("Content-Type", "application/json");

  JsonDocument body;
  JsonArray arr = body["uris"].to<JsonArray>();
  for (int i = 0; i < count; i++) arr.add(uris[i]);
  String json;
  serializeJson(body, json);

  int code = http.PUT(json);
  if (code != 204 && code != 202 && code != 200) {
    Serial.printf("[Spotify] play tracks failed, HTTP %d: %s\n", code, http.getString().c_str());
  } else {
    Serial.println("[Spotify] play tracks OK");
  }
  http.end();
  return code == 204 || code == 202 || code == 200;
}
