#include "playlists_ui.h"
#include "lvgl_v8_port.h"

#include <lvgl.h>

// Provided by the main .ino - the one shared SpotifyClient used elsewhere too.
extern SpotifyClient spotify;

#define COLOR_BG       0x121212
#define COLOR_SURFACE  0x282828
#define COLOR_ACCENT   0x1DB954
#define COLOR_TEXT     0xFFFFFF
#define COLOR_TEXT_DIM 0xB3B3B3

static lv_obj_t *mainScreen = nullptr;
static lv_obj_t *libraryScreen = nullptr;
static lv_obj_t *libraryList = nullptr;
static lv_obj_t *tracksScreen = nullptr;
static lv_obj_t *tracksList = nullptr;
static lv_obj_t *tracksTitleLabel = nullptr;

static bool s_active = false;

// ---- Fetched data (fixed-size, no dynamic containers) ----
static PlaylistBrief s_playlists[SPOTIFY_MAX_PLAYLISTS];
static int s_playlistCount = 0;
// Liked Songs only - Spotify's API blocks reading a regular playlist's track
// list for apps like this one (see the note on getPlaylists() in spotify_client.h),
// so tracks screen only ever shows Liked Songs.
static TrackBrief s_tracks[SPOTIFY_MAX_TRACKS];
static int s_trackCount = 0;
// Scratch space for playTrackUris() - not stack-allocated, since the loop task's
// stack is small enough that this codebase has already hit overflow bugs from
// putting similarly-sized buffers on it (see the comment in lyrics_ui.cpp).
static String s_playUris[SPOTIFY_MAX_TRACKS];

// ---- Pending requests queued by touch, serviced (with blocking network
// calls) from playlistsUI_tick() so the LVGL task never blocks on HTTP. ----
static volatile bool s_openLibraryRequested = false;
static volatile bool s_openLikedSongsRequested = false;
static volatile int s_playPlaylistIndex = -1;
static volatile bool s_playPlaylistRequested = false;
static volatile int s_playTrackIndex = -1;
static volatile bool s_playTrackRequested = false;

// LVGL's own memory pool is small and fixed (see lv_conf.h), so at most one
// list's worth of rows (~150 widgets) is kept resident at a time - each of
// these frees whichever list isn't about to be shown. populateLibraryScreen()
// rebuilds from the already-fetched s_playlists/s_playlistCount, no network call.
static void populateLibraryScreen();

static void rowOpenLikedSongsCb(lv_event_t *e) {
  Serial.println("[UI] Liked Songs row clicked");
  s_openLikedSongsRequested = true;
}

// A regular playlist row - can't browse its tracks (see note above), so
// tapping it just starts the whole playlist playing from the top.
static void rowPlayPlaylistCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  Serial.printf("[UI] playlist row clicked, idx=%d\n", idx);
  s_playPlaylistIndex = idx;
  s_playPlaylistRequested = true;
}

static void rowPlayTrackCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  Serial.printf("[UI] liked-song row clicked, idx=%d\n", idx);
  s_playTrackIndex = idx;
  s_playTrackRequested = true;
}

static void btnBackToNowPlayingCb(lv_event_t *e) {
  lv_obj_clean(libraryList);  // not needed while on Now Playing - free it
  if (mainScreen) lv_scr_load(mainScreen);
  s_active = false;
}

static void btnBackToLibraryCb(lv_event_t *e) {
  lv_obj_clean(tracksList);   // not needed while on the library screen - free it
  populateLibraryScreen();
  lv_scr_load(libraryScreen);
}

static lv_obj_t *createBackButton(lv_obj_t *parent, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 50, 50);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 20, 20);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, LV_SYMBOL_LEFT);
  lv_obj_center(lbl);
  return btn;
}

static lv_obj_t *createListContainer(lv_obj_t *parent) {
  lv_obj_t *list = lv_obj_create(parent);
  lv_obj_set_size(list, 760, 380);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 10, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  return list;
}

// One tappable row with a primary label and an optional dim secondary label.
static lv_obj_t *createRow(lv_obj_t *parent, const char *primary, const String &secondary,
                            lv_event_cb_t cb, int index) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), secondary.length() ? 64 : 50);
  lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_SURFACE), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_hor(row, 16, 0);
  lv_obj_set_style_pad_ver(row, 8, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

  lv_obj_t *nameLbl = lv_label_create(row);
  lv_label_set_text(nameLbl, primary);
  lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nameLbl, LV_PCT(100));
  lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(nameLbl, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_align(nameLbl, LV_ALIGN_TOP_LEFT, 0, 4);

  if (secondary.length() > 0) {
    lv_obj_t *subLbl = lv_label_create(row);
    lv_label_set_text(subLbl, secondary.c_str());
    lv_label_set_long_mode(subLbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(subLbl, LV_PCT(100));
    lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subLbl, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(subLbl, LV_ALIGN_BOTTOM_LEFT, 0, -2);
  }

  return row;
}

static void showEmptyRow(lv_obj_t *parent, const char *text) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT_DIM), 0);
}

static void populateLibraryScreen() {
  lv_obj_clean(libraryList);

  lv_obj_t *likedRow = createRow(libraryList, "Liked Songs", "", rowOpenLikedSongsCb, 0);
  lv_obj_t *likedNameLbl = lv_obj_get_child(likedRow, 0);
  lv_obj_set_style_text_color(likedNameLbl, lv_color_hex(COLOR_ACCENT), 0);

  if (s_playlistCount == 0) {
    showEmptyRow(libraryList, "Couldn't load your playlists.");
  }

  for (int i = 0; i < s_playlistCount; i++) {
    String sub = String(s_playlists[i].trackCount) + " songs - tap to play";
    createRow(libraryList, s_playlists[i].name.c_str(), sub, rowPlayPlaylistCb, i);
  }
}

static void populateLikedSongsScreen() {
  lv_label_set_text(tracksTitleLabel, "Liked Songs");
  lv_obj_clean(tracksList);

  if (s_trackCount == 0) {
    showEmptyRow(tracksList, "No liked songs found.");
    return;
  }

  for (int i = 0; i < s_trackCount; i++) {
    createRow(tracksList, s_tracks[i].name.c_str(), s_tracks[i].artistName, rowPlayTrackCb, i);
  }
}

void playlistsUI_init() {
  mainScreen = lv_scr_act();

  // ---- Library screen: Liked Songs + all playlists ----
  libraryScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(libraryScreen, lv_color_hex(COLOR_BG), 0);
  createBackButton(libraryScreen, btnBackToNowPlayingCb);

  lv_obj_t *libTitle = lv_label_create(libraryScreen);
  lv_label_set_text(libTitle, "Your Library");
  lv_obj_set_style_text_font(libTitle, &lv_font_montserrat_16, 0);
  lv_obj_align(libTitle, LV_ALIGN_TOP_MID, 0, 34);

  libraryList = createListContainer(libraryScreen);

  // ---- Tracks screen: the tracks of whichever playlist (or Liked Songs) was tapped ----
  tracksScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(tracksScreen, lv_color_hex(COLOR_BG), 0);
  createBackButton(tracksScreen, btnBackToLibraryCb);

  tracksTitleLabel = lv_label_create(tracksScreen);
  lv_obj_set_width(tracksTitleLabel, 600);
  lv_label_set_long_mode(tracksTitleLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(tracksTitleLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(tracksTitleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(tracksTitleLabel, LV_ALIGN_TOP_MID, 0, 34);
  lv_label_set_text(tracksTitleLabel, "");

  tracksList = createListContainer(tracksScreen);
}

void playlistsUI_requestOpen() {
  s_openLibraryRequested = true;
}

void playlistsUI_tick() {
  if (s_openLibraryRequested) {
    s_openLibraryRequested = false;

    int count = 0;
    bool ok = spotify.getPlaylists(s_playlists, SPOTIFY_MAX_PLAYLISTS, count);
    s_playlistCount = ok ? count : 0;

    lvgl_port_lock(-1);
    mainScreen = lv_scr_act();
    populateLibraryScreen();
    lv_scr_load(libraryScreen);
    lvgl_port_unlock();
    s_active = true;
    return;  // one blocking network call per loop() pass, same as playback buttons
  }

  if (s_openLikedSongsRequested) {
    s_openLikedSongsRequested = false;

    int count = 0;
    bool ok = spotify.getLikedSongs(s_tracks, SPOTIFY_MAX_TRACKS, count);
    s_trackCount = ok ? count : 0;

    lvgl_port_lock(-1);
    lv_obj_clean(libraryList);  // not needed while on the tracks screen - free it
    populateLikedSongsScreen();
    lv_scr_load(tracksScreen);
    lvgl_port_unlock();
    return;
  }

  if (s_playPlaylistRequested) {
    s_playPlaylistRequested = false;
    int index = s_playPlaylistIndex;

    if (index >= 0 && index < s_playlistCount) {
      spotify.playPlaylist(s_playlists[index].id);
    }

    lvgl_port_lock(-1);
    lv_obj_clean(libraryList);  // heading back to Now Playing - free both lists
    lv_obj_clean(tracksList);
    if (mainScreen) lv_scr_load(mainScreen);
    lvgl_port_unlock();
    s_active = false;
    return;
  }

  if (s_playTrackRequested) {
    s_playTrackRequested = false;
    int index = s_playTrackIndex;

    if (index >= 0 && index < s_trackCount) {
      // Liked Songs has no context_uri - send the remaining uris explicitly
      // so playback continues past the tapped track instead of stopping.
      int n = 0;
      for (int i = index; i < s_trackCount; i++) s_playUris[n++] = s_tracks[i].uri;
      spotify.playTrackUris(s_playUris, n);
    }

    lvgl_port_lock(-1);
    lv_obj_clean(libraryList);  // heading back to Now Playing - free both lists
    lv_obj_clean(tracksList);
    if (mainScreen) lv_scr_load(mainScreen);
    lvgl_port_unlock();
    s_active = false;
    return;
  }
}

bool playlistsUI_isActive() {
  return s_active;
}
