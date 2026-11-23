# ESP8266 NightLight

An ESP8266 RTOS SDK firmware project for an RGB LED nightlight built around SK9822 LEDs. On boot it initializes NVS, loads stored configuration, and brings up WiFi in STA mode from stored credentials. Setup mode can be enabled on demand by holding the two brightness buttons (GPIO 13 and 14) for 5 seconds (or is restored if it was left enabled); it brings up a simultaneous SoftAP (`NightLight-Setup`, open) hosting a configuration web page at `http://192.168.4.1/` for entering WiFi and MQTT credentials. While setup mode is active the LED blinks red and normal output is paused; the web server stays alive while a client is connected or for 30 seconds after the last request. When a STA connection is configured and obtains an IP, an MQTT client connects and subscribes to control topics. Three buttons cycle color and adjust brightness (GPIO 12/13/14).

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

Config is stored in NVS namespace `nightlight` with keys `wifi_ssid`, `wifi_pass`, `mqtt_uri`, `mqtt_user`, `mqtt_pass`, and `setup_enabled`. The config page scans for nearby WiFi networks (showing SSID and RSSI), allows selecting one or entering an SSID manually, and collects the WiFi password, MQTT URI, MQTT username, and MQTT password. A network scan is also available via GET `/scan` (JSON). Submit via POST `/save` on the config page (device reboots after saving and clears setup mode). Defaults to `mqtt://localhost:1883` when no MQTT URI is set.

## MQTT topics

Topics are scoped per device using the device MAC (lowercase, no colons) as `<id>`.

| Topic | Direction | Description |
| --- | --- | --- |
| `nightlight/<id>/color/command` | subscribe | Set color by name, 0-6 index, or RGB code |
| `nightlight/<id>/brightness/command` | subscribe | Set brightness 0-31 |
| `nightlight/<id>/power/command` | subscribe | Turn light on/off |
| `nightlight/<id>/color/state` | publish (retained) | Current color |
| `nightlight/<id>/brightness/state` | publish (retained) | Current brightness |
| `nightlight/<id>/power/state` | publish (retained) | `on`/`off` |
| `nightlight/<id>/availability` | publish (retained, LWT) | `online`/`offline` |
| `nightlight/<id>/info` | publish (retained) | JSON device info |

### Available commands

Color (`nightlight/<id>/color/command`): payload may be a color name, numeric index (0-6), or RGB code.

RGB: payload may be a hex code (`#FF0000`, `FF0000`) or comma/space separated values (`255,0,0`, `255 0 0`). Also accepted on the color command topic.

| Name | Index | RGB |
| --- | --- | --- |
| red | 0 | 255,0,0 |
| green | 1 | 0,255,0 |
| blue | 2 | 0,0,255 |
| white | 3 | 255,255,255 |
| yellow | 4 | 255,255,0 |
| cyan | 5 | 0,255,255 |
| magenta | 6 | 255,0,255 |

Brightness (`nightlight/<id>/brightness/command`): integer 0-31.

Power (`nightlight/<id>/power/command`): `on`/`off`, `1`/`0`, or `true`/`false`. Turning on restores the current color/brightness.

State topics publish the current value (`red`, `#FF0000`, or `16`). Availability publishes `online`/`offline` (retained, LWT).

### Topic details

- `<id>` is the device MAC address in lowercase hex with no colons (e.g. `aabbccddeeff`).
- Command topics are subscribed; publish payloads to them to change state.
- State topics are published retained on every change and on connect.
- `availability` is retained with LWT `offline`; `online` is published on connect.
- `info` is retained and republished on connect and every 60 seconds.

### Example payloads

Color command:
```
nightlight/aabbccddeeff/color/command  ->  red
nightlight/aabbccddeeff/color/command  ->  3
nightlight/aabbccddeeff/color/command  ->  #FF0000
nightlight/aabbccddeeff/color/command  ->  255,0,0
```

Brightness command:
```
nightlight/aabbccddeeff/brightness/command  ->  16
```

Power command:
```
nightlight/aabbccddeeff/power/command  ->  off
nightlight/aabbccddeeff/power/command  ->  on
```

State:
```
nightlight/aabbccddeeff/color/state        ->  red
nightlight/aabbccddeeff/brightness/state   ->  16
nightlight/aabbccddeeff/power/state        ->  on
```

Availability:
```
nightlight/aabbccddeeff/availability      ->  online
```

Info:
```json
{"status":"online","version":"...","heap":12345,"uptime":3600,"rssi":-42,"ip":"192.168.1.42","mac":"aa:bb:cc:dd:ee:ff"}
```
