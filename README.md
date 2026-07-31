# Spotify Now Playing Display

A Spotify "now playing" display for the Waveshare ESP32-S3-Touch-LCD-4.3B (800x480,
capacitive touch). Shows the current track, artist, album art, and progress bar;
lets you play/pause/skip from the touchscreen; and can pull up live, time-synced
lyrics for the track that's playing.

## Features

- Album art, track name, artist name, and progress bar, refreshed every few seconds
- Touch controls for play/pause, next, and previous
- A "Lyrics" screen with time-synced lyrics (via [lrclib.net](https://lrclib.net)),
  auto-scrolling and highlighting the current line as the track plays
- Dark UI styled after Spotify's own color palette

## Hardware

- [Waveshare ESP32-S3-Touch-LCD-4.3B](https://www.waveshare.com/esp32-s3-touch-lcd-4.3B.htm)
  (800x480 RGB LCD, GT911 capacitive touch, ESP32-S3 with 8MB PSRAM)

## Required libraries (Arduino IDE Library Manager)

- `ESP32_Display_Panel`
- `esp-lib-utils`
- `ESP32_IO_Expander`
- `lvgl` (v8.4.x)
- `ArduinoJson` (v7.x)
- `TJpg_Decoder`

## Setup

1. Install the libraries above via the Arduino IDE Library Manager.
2. Copy `secrets.h.example` to `secrets.h` and fill in:
   - Your WiFi SSID/password
   - A Spotify app's client ID/secret (from the
     [Spotify Developer Dashboard](https://developer.spotify.com/dashboard))
   - A refresh token for your Spotify account, scoped for
     `user-read-currently-playing`, `user-read-playback-state`, and
     `user-modify-playback-state` (there are various one-off scripts online for
     generating this via Spotify's OAuth flow - `secrets.h` is gitignored so it's
     safe to keep your real token there).
3. In Arduino IDE, select **ESP32S3 Dev Module** as the board and set:

   | Setting | Value |
   |---|---|
   | PSRAM | OPI |
   | Flash Mode | QIO 80MHz |
   | Flash Size | 16MB |
   | USB CDC On Boot | Enabled |
   | Partition Scheme | Any scheme with a 3MB+ app partition (e.g. "16M Flash (3MB APP/9.9MB FATFS)") |

4. Verify and upload.

## Notes / troubleshooting

- If you see screen tearing/drift on the RGB LCD, this is a known ESP32-S3 +
  RGB-LCD issue; the bounce buffer size in the `.ino` is already tuned up for it,
  but see [ESP32_Display_Panel's troubleshooting guide](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/docs/envs/use_with_arduino.md#solution-for-screen-drift-issue-when-using-esp32-s3-to-drive-rgb-lcd-in-arduino-ide)
  if it persists.
- Touch and the board's resolution/variant (800x480 vs. 1024x600) are configured
  in `esp_panel_board_custom_conf.h`.
- If the lyrics screen shows "No synced lyrics found," that track just isn't in
  lrclib.net's (community-sourced) database - not every track has one.
