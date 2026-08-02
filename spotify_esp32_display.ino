#include <Arduino.h>
#include <esp_display_panel.hpp>

#include <lvgl.h>
#include "lvgl_v8_port.h"
#include <demos/lv_demos.h>

#include <WiFi.h>
#include "secrets.h"
#include "spotify_client.h"
#include "now_playing_ui.h"
#include "lyrics_ui.h"
#include "playlists_ui.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/**
 * To use the built-in examples and demos of LVGL uncomment the includes below respectively.
 */
 // #include <demos/lv_demos.h>
 // #include <examples/lv_examples.h>

SpotifyClient spotify;
unsigned long lastPollMs = 0;
const unsigned long POLL_INTERVAL_MS = 3000;  // poll Spotify every 3 seconds

void setup()
{
    String title = "LVGL porting example";

    Serial.begin(115200);

    Serial.println("Initializing board");
    Board *board = new Board();
    board->init();

    #if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    /**
     * As the anti-tearing feature typically consumes more PSRAM bandwidth, for the ESP32-S3, we need to utilize the
     * "bounce buffer" functionality to enhance the RGB data bandwidth.
     * This feature will consume `bounce_buffer_size * bytes_per_pixel * 2` of SRAM memory.
     */
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        // ESP32-S3 RGB LCDs can show "screen drift"/tearing; ESP32_Display_Panel's own
        // troubleshooting guide recommends bumping this from the default *10 to *20.
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 20);
    }
#endif
#endif
    assert(board->begin());

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating UI");
    /* Lock the mutex due to the LVGL APIs are not thread-safe */
    lvgl_port_lock(-1);

    lv_theme_t *theme = lv_theme_default_init(
        lv_disp_get_default(),
        lv_palette_main(LV_PALETTE_GREEN),
        lv_palette_main(LV_PALETTE_GREY),
        true,  // dark mode
        LV_FONT_DEFAULT);
    lv_disp_set_theme(lv_disp_get_default(), theme);

    nowPlayingUI_init();   // builds the track/artist/art/buttons on the LVGL screen
    lyricsUI_init();       // builds the (initially hidden) lyrics screen
    playlistsUI_init();    // builds the (initially hidden) library/playlist screens

    /* Release the mutex */
    lvgl_port_unlock();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.println(" connected!");

    spotify.begin();  // exchanges refresh token for a first access token
}

void loop()
{
    // Note: lvgl_v8_port.cpp already runs lv_timer_handler() on its own FreeRTOS
    // task (lvgl_port_task), so it must NOT be called again here.

    // Runs any playback action a button queued (see now_playing_ui.cpp for why
    // this can't happen directly in the LVGL click callback). Deliberately does
    // NOT chain an immediate follow-up poll here - stacking two blocking network
    // calls back-to-back in one loop() pass made button presses feel slower, not
    // faster. The regular POLL_INTERVAL_MS cadence below picks up the change.
    nowPlayingUI_processPendingAction();

    // Same reasoning as above: services one queued library/playlist/track
    // request per pass (open library, open a playlist, or play a track),
    // each of which is a blocking network call.
    playlistsUI_tick();

    if (millis() - lastPollMs > POLL_INTERVAL_MS) {
        lastPollMs = millis();
        NowPlaying np;
        if (spotify.getNowPlaying(np)) {
            /* Lock the mutex due to the LVGL APIs are not thread-safe */
            lvgl_port_lock(-1);
            nowPlayingUI_update(np);
            lvgl_port_unlock();

            // Locks internally around its own LVGL calls; the lyrics fetch
            // itself runs unlocked so it doesn't stall LVGL rendering/touch.
            lyricsUI_onNowPlaying(np);
        }
    }

    // Cheap no-op unless the lyrics screen is showing; locks internally.
    lyricsUI_tick();

    delay(5);
}
