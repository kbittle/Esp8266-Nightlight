# ESP8266 NightLight

An ESP8266 RTOS SDK firmware project for an RGB LED nightlight built around SK9822 LEDs. On boot it initializes NVS, loads stored configuration, and brings up WiFi in simultaneous AP+STA mode. A SoftAP (`NightLight-Setup`, open) hosts a configuration web page at `http://192.168.4.1/` for entering WiFi and MQTT credentials. When a STA connection is configured and obtains an IP, an MQTT client connects and subscribes to control topics. Three buttons cycle color and adjust brightness (GPIO 12/13/14).

## Pinout

| Function | GPIO |
| --- | --- |
| SK9822 data | 2 |
| SK9822 clock | 16 |
| Color cycle button | 12 |
| Brightness up button | 13 |
| Brightness down button | 14 |

Buttons are active-low with internal pull-ups.

## Build
Source the SDK environment first, then build:

```sh
source /home/kristopherbittle/esp/ESP8266_RTOS_SDK/export.sh
make
```

Flash with `make flash` (default port `/dev/ttyUSB0`). Full clean with `make clean`.

## Configuration

Config is stored in NVS namespace `nightlight` with keys `wifi_ssid`, `wifi_pass`, `mqtt_uri`, `mqtt_user`, `mqtt_pass`. Submit via POST `/save` on the config page (device reboots after saving). Defaults to `mqtt://localhost:1883` when no MQTT URI is set.

## MQTT topics

| Topic | Direction | Description |
| --- | --- | --- |
| `nightlight/color` | subscribe | Set color by name, 0-6 index, or RGB code |
| `nightlight/brightness` | subscribe | Set brightness 0-31 |
| `nightlight/rgb` | subscribe | Set custom RGB color code |
| `nightlight/status` | publish (retained) | JSON status on connect |

### Available commands

Color (`nightlight/color`): payload may be a color name, numeric index (0-6), or RGB code.

RGB (`nightlight/rgb`): payload may be a hex code (`#FF0000`, `FF0000`) or comma/space separated values (`255,0,0`, `255 0 0`). Also accepted on `nightlight/color`.

| Name | Index | RGB |
| --- | --- | --- |
| red | 0 | 255,0,0 |
| green | 1 | 0,255,0 |
| blue | 2 | 0,0,255 |
| white | 3 | 255,255,255 |
| yellow | 4 | 255,255,0 |
| cyan | 5 | 0,255,255 |
| magenta | 6 | 255,0,255 |

Brightness (`nightlight/brightness`): integer 0-31.

Status payload (`nightlight/status`) example:

```json
{"status":"online","version":"...","heap":12345,"color":"red","brightness":16,"mac":"aa:bb:cc:dd:ee:ff"}
```
