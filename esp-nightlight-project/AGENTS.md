# AGENTS.md

## Build

- ESP8266 RTOS SDK project. Source the SDK environment first:
  `source /home/kristopherbittle/esp/ESP8266_RTOS_SDK/export.sh`
- Build: `make`
- Full clean: `make clean` (removes `build/`)
- Target is `esp8266` (`sdkconfig`); flash via `make flash` (default port `/dev/ttyUSB0`).

## Structure

- App entrypoint: `main/main.c` (`app_main`); LED driver in `main/sk9822.c`.
- `main/CMakeLists.txt` declares REQUIRES for `esp_http_server`, `mqtt`, `nvs_flash`, `esp_wifi`, `tcpip_adapter`, `esp_event`.

## Runtime behavior

- On boot: init NVS, load config from namespace `nightlight`, init WiFi AP+STA.
- SoftAP SSID `NightLight-Setup` (open), config page at `http://192.168.4.1/`.
- STA connects only if `wifi_ssid` is stored; MQTT starts after STA gets IP using `mqtt_uri`.
- Config saved via POST `/save` writes NVS keys (`wifi_ssid`, `wifi_pass`, `mqtt_uri`, `mqtt_user`, `mqtt_pass`) then reboots.
- MQTT control topics: `nightlight/color` (color name, 0-6 index, or RGB code), `nightlight/brightness` (0-31), `nightlight/rgb` (RGB color code, e.g. `#FF0000` or `255,0,0`).

## Notes

- No tests in repo.
- `sdkconfig` is committed; partition table is single-app with NVS included.