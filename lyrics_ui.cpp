#include "lyrics_ui.h"
#include "lyrics_client.h"
#include "lvgl_v8_port.h"

#include <lvgl.h>

#define COLOR_BG       0x121212
#define COLOR_SURFACE  0x282828
#define COLOR_ACCENT   0x1DB954
#define COLOR_TEXT_DIM 0xB3B3B3

static LyricsClient lyricsClient;

static lv_obj_t *lyricsScreen = nullptr;
static lv_obj_t *mainScreen = nullptr;
static lv_obj_t *titleLabel = nullptr;
static lv_obj_t *linesContainer = nullptr;
static lv_obj_t *statusLabel = nullptr;

static LyricLine lyricLines[LYRICS_MAX_LINES];
static lv_obj_t *lineLabels[LYRICS_MAX_LINES];
static int lyricLineCount = 0;
static int activeLineIndex = -1;

static bool s_active = false;
static volatile bool s_openRequested = false;

static String s_loadedTrackKey;
static String s_lastTrackName;
static String s_lastArtistName;
static long s_lastDurationMs = 0;
static long s_lastProgressMs = 0;
static unsigned long s_lastProgressAtMs = 0;

static void btnBackCb(lv_event_t *e) {
  if (mainScreen) {
    lv_scr_load(mainScreen);
  }
  s_active = false;
}

static void clearLines() {
  lv_obj_clean(linesContainer);
  lyricLineCount = 0;
  activeLineIndex = -1;
  statusLabel = nullptr;
}

// Fetches + rebuilds the line list for the currently cached track.
// The network fetch writes straight into the static `lyricLines` buffer (not
// the stack - it's ~14KB, too big for the loop task's stack) and happens
// before we touch any LVGL object, so the LVGL lock is only held briefly.
static void loadLyricsForCurrentTrack() {
  int freshCount = 0;
  bool ok = lyricsClient.fetchSynced(s_lastTrackName, s_lastArtistName, s_lastDurationMs / 1000,
                                      lyricLines, LYRICS_MAX_LINES, freshCount);

  lvgl_port_lock(-1);

  clearLines();
  lv_label_set_text(titleLabel, (s_lastTrackName + "  -  " + s_lastArtistName).c_str());

  if (!ok || freshCount == 0) {
    statusLabel = lv_label_create(linesContainer);
    lv_label_set_text(statusLabel, "No synced lyrics found for this track.");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
    lvgl_port_unlock();
    return;
  }

  lyricLineCount = freshCount;
  for (int i = 0; i < lyricLineCount; i++) {
    lv_obj_t *lbl = lv_label_create(linesContainer);
    lv_label_set_text(lbl, lyricLines[i].text.length() ? lyricLines[i].text.c_str() : " ");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_set_width(lbl, LV_PCT(90));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lineLabels[i] = lbl;
  }

  lvgl_port_unlock();
}

void lyricsUI_init() {
  mainScreen = lv_scr_act();

  lyricsScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(lyricsScreen, lv_color_hex(COLOR_BG), 0);

  lv_obj_t *btnBack = lv_btn_create(lyricsScreen);
  lv_obj_set_size(btnBack, 50, 50);
  lv_obj_set_style_radius(btnBack, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btnBack, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_shadow_width(btnBack, 0, 0);
  lv_obj_align(btnBack, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_add_event_cb(btnBack, btnBackCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lblBack = lv_label_create(btnBack);
  lv_label_set_text(lblBack, LV_SYMBOL_LEFT);
  lv_obj_center(lblBack);

  titleLabel = lv_label_create(lyricsScreen);
  lv_obj_set_width(titleLabel, 600);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 34);
  lv_label_set_text(titleLabel, "");

  linesContainer = lv_obj_create(lyricsScreen);
  lv_obj_set_size(linesContainer, 760, 380);
  lv_obj_align(linesContainer, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(linesContainer, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(linesContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(linesContainer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(linesContainer, 0, 0);
  lv_obj_set_style_pad_row(linesContainer, 14, 0);
}

void lyricsUI_requestOpen() {
  s_openRequested = true;
}

void lyricsUI_onNowPlaying(const NowPlaying &np) {
  bool trackChanged = np.hasTrack && (np.trackName != s_lastTrackName || np.artistName != s_lastArtistName);

  s_lastTrackName = np.trackName;
  s_lastArtistName = np.artistName;
  s_lastDurationMs = np.durationMs;
  s_lastProgressMs = np.progressMs;
  s_lastProgressAtMs = millis();

  bool wantsOpen = s_openRequested;
  s_openRequested = false;

  bool needsRefetch = wantsOpen || (s_active && trackChanged);

  if (!needsRefetch || !np.hasTrack) return;

  if (wantsOpen) {
    lvgl_port_lock(-1);
    mainScreen = lv_scr_act();
    lv_scr_load(lyricsScreen);
    lvgl_port_unlock();
    s_active = true;
  }

  loadLyricsForCurrentTrack();
  s_loadedTrackKey = np.trackName + "|" + np.artistName;
}

void lyricsUI_tick() {
  if (!s_active || lyricLineCount == 0) return;

  long estimated = s_lastProgressMs + (long)(millis() - s_lastProgressAtMs);

  int newIndex = -1;
  for (int i = 0; i < lyricLineCount; i++) {
    if (lyricLines[i].timeMs <= estimated) {
      newIndex = i;
    } else {
      break;
    }
  }

  if (newIndex == activeLineIndex) return;

  lvgl_port_lock(-1);

  if (activeLineIndex >= 0 && activeLineIndex < lyricLineCount) {
    lv_obj_set_style_text_color(lineLabels[activeLineIndex], lv_color_hex(COLOR_TEXT_DIM), 0);
  }
  if (newIndex >= 0) {
    lv_obj_set_style_text_color(lineLabels[newIndex], lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_scroll_to_view(lineLabels[newIndex], LV_ANIM_ON);
  }
  activeLineIndex = newIndex;

  lvgl_port_unlock();
}

bool lyricsUI_isActive() {
  return s_active;
}
