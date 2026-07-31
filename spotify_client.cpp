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
  http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing");
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
