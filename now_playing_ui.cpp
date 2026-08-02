#include "now_playing_ui.h"
#include "lyrics_ui.h"
#include "playlists_ui.h"

#include <lvgl.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>

// Provided by the main .ino - the one shared SpotifyClient used for playback buttons.
extern SpotifyClient spotify;

// ---- Colors (Spotify-ish dark palette) ----
#define COLOR_BG       0x121212
#define COLOR_SURFACE  0x282828
#define COLOR_ACCENT   0x1DB954
#define COLOR_TEXT     0xFFFFFF
#define COLOR_TEXT_DIM 0xB3B3B3

// ---- Widgets ----
static lv_obj_t *artImg;
static lv_obj_t *trackLabel;
static lv_obj_t *artistLabel;
static lv_obj_t *progressBar;
static lv_obj_t *lblPlayPauseIcon;

// ---- Album art decode buffer (lives in PSRAM) ----
// Sized for up to 320x320 @ 16bpp (RGB565). Spotify's mid-size art is ~300x300.
#define ART_MAX_DIM 320
static lv_color_t *artBuf = nullptr;
static lv_img_dsc_t artDesc;
static String lastArtUrl = "";

// TJpg_Decoder calls this once per decoded block of pixels.
static bool jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= ART_MAX_DIM) return false;  // safety: ignore anything past our buffer
  for (int16_t row = 0; row < h; row++) {
    int destY = y + row;
    if (destY >= ART_MAX_DIM) break;
    for (int16_t col = 0; col < w; col++) {
      int destX = x + col;
      if (destX >= ART_MAX_DIM) continue;
      artBuf[destY * ART_MAX_DIM + destX].full = bitmap[row * w + col];
    }
  }
  return true;
}

static void downloadAndDecodeArt(const String &url) {
  if (url.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[Art] download failed HTTP %d\n", code);
    http.end();
    return;
  }

  int len = http.getSize();
  uint8_t *jpegBuf = (uint8_t *)ps_malloc(len);
  if (!jpegBuf) {
    Serial.println("[Art] out of PSRAM for jpeg buffer");
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  int readTotal = 0;
  while (readTotal < len && http.connected()) {
    int avail = stream->available();
    if (avail) {
      int r = stream->read(jpegBuf + readTotal, len - readTotal);
      if (r > 0) readTotal += r;
    } else {
      delay(1);
    }
  }
  http.end();

  // Clear buffer to black before decoding new art (avoids leftover pixels if new
  // image is smaller than the previous one).
  memset(artBuf, 0, ART_MAX_DIM * ART_MAX_DIM * sizeof(lv_color_t));

  TJpgDec.drawJpg(0, 0, jpegBuf, readTotal);
  free(jpegBuf);

  artDesc.header.always_zero = 0;
  artDesc.header.w = ART_MAX_DIM;
  artDesc.header.h = ART_MAX_DIM;
  artDesc.data_size = ART_MAX_DIM * ART_MAX_DIM * sizeof(lv_color_t);
  artDesc.header.cf = LV_IMG_CF_TRUE_COLOR;
  artDesc.data = (const uint8_t *)artBuf;

  lv_img_set_src(artImg, &artDesc);
}

// ---- Playback button callbacks ----
// These run on the LVGL task, so they must NOT make blocking network calls
// directly (that would freeze touch/rendering until the request finishes).
// Instead they just queue the action; nowPlayingUI_processPendingAction()
// (called from loop(), off the LVGL task) does the actual SpotifyClient call.
enum class PlaybackAction { None, Play, Pause, Next, Previous };
static volatile PlaybackAction s_pendingAction = PlaybackAction::None;

static void btnPrevCb(lv_event_t *e) {
  Serial.println("[UI] prev button clicked");
  s_pendingAction = PlaybackAction::Previous;
}
static void btnPlayPauseCb(lv_event_t *e) {
  bool *isPlayingFlag = (bool *)lv_event_get_user_data(e);
  Serial.printf("[UI] play/pause button clicked, isPlaying=%d\n", *isPlayingFlag);
  s_pendingAction = *isPlayingFlag ? PlaybackAction::Pause : PlaybackAction::Play;
}
static void btnNextCb(lv_event_t *e) {
  Serial.println("[UI] next button clicked");
  s_pendingAction = PlaybackAction::Next;
}
static void btnLyricsCb(lv_event_t *e) {
  Serial.println("[UI] lyrics button clicked");
  lyricsUI_requestOpen();
}
static void btnPlaylistsCb(lv_event_t *e) {
  Serial.println("[UI] playlists button clicked");
  playlistsUI_requestOpen();
}

static bool s_isPlaying = false;

// Creates a round icon button with the given diameter, background color, and symbol.
static lv_obj_t *createIconButton(lv_obj_t *parent, int diameter, uint32_t bgColor, const char *symbol,
                                   lv_event_cb_t cb, void *userData, lv_obj_t **outIconLabel = nullptr) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, diameter, diameter);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, symbol);
  lv_obj_center(lbl);
  if (outIconLabel) *outIconLabel = lbl;

  return btn;
}

void nowPlayingUI_init() {
  artBuf = (lv_color_t *)ps_malloc(ART_MAX_DIM * ART_MAX_DIM * sizeof(lv_color_t));
  memset(artBuf, 0, ART_MAX_DIM * ART_MAX_DIM * sizeof(lv_color_t));

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpegOutputCallback);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);

  const int artSize = 280;
  artImg = lv_img_create(scr);
  lv_obj_set_size(artImg, artSize, artSize);
  lv_obj_set_style_radius(artImg, 16, 0);
  lv_obj_set_style_clip_corner(artImg, true, 0);
  lv_obj_set_style_bg_color(artImg, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(artImg, LV_OPA_COVER, 0);
  lv_obj_align(artImg, LV_ALIGN_LEFT_MID, 40, 0);

  const int colX = 360;
  const int colW = 400;

  trackLabel = lv_label_create(scr);
  lv_obj_set_width(trackLabel, colW);
  lv_label_set_long_mode(trackLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(trackLabel, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(trackLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_align(trackLabel, LV_ALIGN_TOP_LEFT, colX, 100);
  lv_label_set_text(trackLabel, "Nothing playing");

  artistLabel = lv_label_create(scr);
  lv_obj_set_width(artistLabel, colW);
  lv_label_set_long_mode(artistLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(artistLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(artistLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
  lv_obj_align(artistLabel, LV_ALIGN_TOP_LEFT, colX, 148);
  lv_label_set_text(artistLabel, "");

  progressBar = lv_bar_create(scr);
  lv_obj_set_size(progressBar, colW, 6);
  lv_obj_set_style_radius(progressBar, 3, 0);
  lv_obj_set_style_bg_color(progressBar, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_color(progressBar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(progressBar, 3, LV_PART_INDICATOR);
  lv_obj_align(progressBar, LV_ALIGN_TOP_LEFT, colX, 210);
  lv_bar_set_range(progressBar, 0, 1000);

  const int playDiam = 90;
  const int sideDiam = 64;
  const int gap = 24;
  const int controlsY = 260;
  const int playX = colX + colW / 2 - playDiam / 2;

  lv_obj_t *btnPrev = createIconButton(scr, sideDiam, COLOR_SURFACE, LV_SYMBOL_PREV, btnPrevCb, nullptr);
  lv_obj_align(btnPrev, LV_ALIGN_TOP_LEFT, playX - gap - sideDiam, controlsY + (playDiam - sideDiam) / 2);

  lv_obj_t *btnPlay = createIconButton(scr, playDiam, COLOR_ACCENT, LV_SYMBOL_PLAY, btnPlayPauseCb, &s_isPlaying,
                                        &lblPlayPauseIcon);
  lv_obj_set_style_text_color(lblPlayPauseIcon, lv_color_hex(COLOR_BG), 0);
  lv_obj_align(btnPlay, LV_ALIGN_TOP_LEFT, playX, controlsY);

  lv_obj_t *btnNext = createIconButton(scr, sideDiam, COLOR_SURFACE, LV_SYMBOL_NEXT, btnNextCb, nullptr);
  lv_obj_align(btnNext, LV_ALIGN_TOP_LEFT, playX + playDiam + gap, controlsY + (playDiam - sideDiam) / 2);

  lv_obj_t *btnLyrics = lv_btn_create(scr);
  lv_obj_set_size(btnLyrics, 120, 50);
  lv_obj_set_style_radius(btnLyrics, 25, 0);
  lv_obj_set_style_bg_color(btnLyrics, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_shadow_width(btnLyrics, 0, 0);
  lv_obj_align(btnLyrics, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
  lv_obj_add_event_cb(btnLyrics, btnLyricsCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lblLyrics = lv_label_create(btnLyrics);
  lv_label_set_text(lblLyrics, LV_SYMBOL_LIST " Lyrics");
  lv_obj_set_style_text_color(lblLyrics, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_center(lblLyrics);

  lv_obj_t *btnPlaylists = lv_btn_create(scr);
  lv_obj_set_size(btnPlaylists, 150, 50);
  lv_obj_set_style_radius(btnPlaylists, 25, 0);
  lv_obj_set_style_bg_color(btnPlaylists, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_shadow_width(btnPlaylists, 0, 0);
  lv_obj_align(btnPlaylists, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_add_event_cb(btnPlaylists, btnPlaylistsCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lblPlaylists = lv_label_create(btnPlaylists);
  lv_label_set_text(lblPlaylists, LV_SYMBOL_DIRECTORY " Playlists");
  lv_obj_set_style_text_color(lblPlaylists, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_center(lblPlaylists);
}

void nowPlayingUI_update(const NowPlaying &np) {
  s_isPlaying = np.isPlaying;
  lv_label_set_text(lblPlayPauseIcon, np.isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

  if (!np.hasTrack) {
    lv_label_set_text(trackLabel, "Nothing playing");
    lv_label_set_text(artistLabel, "");
    lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
    return;
  }

  lv_label_set_text(trackLabel, np.trackName.c_str());
  lv_label_set_text(artistLabel, np.artistName.c_str());

  if (np.durationMs > 0) {
    int pct = (int)((np.progressMs * 1000LL) / np.durationMs);
    lv_bar_set_value(progressBar, pct, LV_ANIM_ON);
  }

  if (np.albumArtUrl.length() > 0 && np.albumArtUrl != lastArtUrl) {
    lastArtUrl = np.albumArtUrl;
    downloadAndDecodeArt(np.albumArtUrl);
  }
}

bool nowPlayingUI_processPendingAction() {
  PlaybackAction action = s_pendingAction;
  if (action == PlaybackAction::None) return false;
  s_pendingAction = PlaybackAction::None;

  switch (action) {
    case PlaybackAction::Play:     spotify.play();     break;
    case PlaybackAction::Pause:    spotify.pause();    break;
    case PlaybackAction::Next:     spotify.next();     break;
    case PlaybackAction::Previous: spotify.previous(); break;
    default: break;
  }
  return true;
}
