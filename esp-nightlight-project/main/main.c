#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "sk9822.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "mqtt_client.h"

#include "tcpip_adapter.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define BTN_COLOR_GPIO        GPIO_NUM_12
#define BTN_BRIGHT_UP_GPIO    GPIO_NUM_13
#define BTN_BRIGHT_DOWN_GPIO  GPIO_NUM_14

static const uint8_t colors[][3] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 255},
    {255, 255, 0},
    {0, 255, 255},
    {255, 0, 255}
};

static const int color_count = sizeof(colors) / sizeof(colors[0]);

static const char *color_names[] = {"red", "green", "blue", "white", "yellow", "cyan", "magenta"};

static int color_index = 0;
static int g_setup_enabled = 0;
static uint8_t brightness = 16;
static uint8_t custom_r = 0;
static uint8_t custom_g = 0;
static uint8_t custom_b = 0;
static int custom_color = 0;
static volatile int g_ap_clients = 0;
static volatile TickType_t g_web_until = 0;

static const char *TAG = "nightlight";

typedef struct {
    char ssid[33];
    char pass[65];
    char mqtt_uri[128];
    char mqtt_user[64];
    char mqtt_pass[64];
} app_config_t;

static app_config_t g_cfg;

static httpd_handle_t g_server = NULL;
static esp_mqtt_client_handle_t g_mqtt = NULL;

static int setup_mode_active(void) {
    if (!g_setup_enabled) return 0;
    return g_ap_clients > 0 || xTaskGetTickCount() < g_web_until;
}

static void update_leds(void) {
    if (setup_mode_active()) return;
    if (custom_color) {
        sk9822_set_color(custom_r, custom_g, custom_b, brightness);
    } else {
        sk9822_set_color(colors[color_index][0], colors[color_index][1],
                         colors[color_index][2], brightness);
    }
}

static void nvs_init_safe(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

#define NVS_NS "nightlight"

static void load_config(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len;
    len = sizeof(g_cfg.ssid); nvs_get_str(h, "wifi_ssid", g_cfg.ssid, &len);
    len = sizeof(g_cfg.pass); nvs_get_str(h, "wifi_pass", g_cfg.pass, &len);
    len = sizeof(g_cfg.mqtt_uri); nvs_get_str(h, "mqtt_uri", g_cfg.mqtt_uri, &len);
    len = sizeof(g_cfg.mqtt_user); nvs_get_str(h, "mqtt_user", g_cfg.mqtt_user, &len);
    len = sizeof(g_cfg.mqtt_pass); nvs_get_str(h, "mqtt_pass", g_cfg.mqtt_pass, &len);
    uint8_t en = 0;
    nvs_get_u8(h, "setup_enabled", &en);
    g_setup_enabled = en ? 1 : 0;
    nvs_close(h);
    if (strlen(g_cfg.mqtt_uri) == 0) {
        strncpy(g_cfg.mqtt_uri, "mqtt://localhost:1883", sizeof(g_cfg.mqtt_uri) - 1);
    }
}

static void save_config(const app_config_t *cfg) {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    nvs_set_str(h, "wifi_ssid", cfg->ssid);
    nvs_set_str(h, "wifi_pass", cfg->pass);
    nvs_set_str(h, "mqtt_uri", cfg->mqtt_uri);
    nvs_set_str(h, "mqtt_user", cfg->mqtt_user);
    nvs_set_str(h, "mqtt_pass", cfg->mqtt_pass);
    nvs_set_u8(h, "setup_enabled", 0);
    nvs_commit(h);
    nvs_close(h);
    g_cfg = *cfg;
    g_setup_enabled = 0;
}

static void url_decode(char *dst, const char *src, size_t dst_size) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di < dst_size - 1; i++) {
        if (src[i] == '+') {
            dst[di++] = ' ';
        } else if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static int get_field(const char *body, const char *key, char *out, size_t out_size) {
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return -1;
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t vlen = end ? (size_t)(end - p) : strlen(p);
    char tmp[256];
    if (vlen >= sizeof(tmp)) vlen = sizeof(tmp) - 1;
    memcpy(tmp, p, vlen);
    tmp[vlen] = '\0';
    url_decode(out, tmp, out_size);
    return 0;
}

static void restart_task(void *pv) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static int color_index_from_name(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "red") == 0) return 0;
    if (strcmp(name, "green") == 0) return 1;
    if (strcmp(name, "blue") == 0) return 2;
    if (strcmp(name, "white") == 0) return 3;
    if (strcmp(name, "yellow") == 0) return 4;
    if (strcmp(name, "cyan") == 0) return 5;
    if (strcmp(name, "magenta") == 0) return 6;
    return -1;
}

static int parse_rgb(const char *input, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!input || !r || !g || !b) return -1;
    char buf[64];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '#') p++;

    size_t len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' ||
           p[len - 1] == '\r' || p[len - 1] == '\n')) {
        p[--len] = '\0';
    }

    int ri, gi, bi;
    if (sscanf(p, "%d , %d , %d", &ri, &gi, &bi) == 3 ||
        sscanf(p, "%d %d %d", &ri, &gi, &bi) == 3) {
        if (ri < 0 || ri > 255 || gi < 0 || gi > 255 || bi < 0 || bi > 255) return -1;
        *r = (uint8_t)ri; *g = (uint8_t)gi; *b = (uint8_t)bi;
        return 0;
    }

    len = strlen(p);
    if (len == 6 || len == 3) {
        for (size_t i = 0; i < len; i++) {
            if (!isxdigit((unsigned char)p[i])) return -1;
        }
        unsigned int rv, gv, bv;
        if (len == 6) {
            if (sscanf(p, "%02x%02x%02x", &rv, &gv, &bv) != 3) return -1;
        } else {
            if (sscanf(p, "%1x%1x%1x", &rv, &gv, &bv) != 3) return -1;
            rv *= 17; gv *= 17; bv *= 17;
        }
        *r = (uint8_t)rv; *g = (uint8_t)gv; *b = (uint8_t)bv;
        return 0;
    }
    return -1;
}

static void set_rgb_color(uint8_t r, uint8_t g, uint8_t b) {
    custom_r = r;
    custom_g = g;
    custom_b = b;
    custom_color = 1;
    update_leds();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "MQTT connected");
            esp_mqtt_client_subscribe(event->client, "nightlight/color", 0);
            esp_mqtt_client_subscribe(event->client, "nightlight/brightness", 0);
            esp_mqtt_client_subscribe(event->client, "nightlight/rgb", 0);

            uint8_t mac[6] = {0};
            esp_base_mac_addr_get(mac);
            char color_str[16];
            if (custom_color) {
                snprintf(color_str, sizeof(color_str), "#%02X%02X%02X", custom_r, custom_g, custom_b);
            } else {
                strncpy(color_str, color_names[color_index >= 0 && color_index < color_count ? color_index : 0], sizeof(color_str) - 1);
                color_str[sizeof(color_str) - 1] = '\0';
            }
            char payload[256];
            snprintf(payload, sizeof(payload),
                "{\"status\":\"online\",\"version\":\"%s\",\"heap\":%u,\"color\":\"%s\",\"brightness\":%u,\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\"}",
                esp_get_idf_version(), (unsigned)esp_get_free_heap_size(),
                color_str,
                (unsigned)brightness,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            esp_mqtt_client_publish(event->client, "nightlight/status", payload, 0, 0, 1);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_DATA: {
            char topic[64];
            char data[128];
            int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
            int dlen = event->data_len < (int)sizeof(data) - 1 ? event->data_len : (int)sizeof(data) - 1;
            memcpy(topic, event->topic, tlen); topic[tlen] = '\0';
            memcpy(data, event->data, dlen); data[dlen] = '\0';

            if (strcmp(topic, "nightlight/color") == 0) {
                uint8_t r, g, b;
                if (parse_rgb(data, &r, &g, &b) == 0) {
                    set_rgb_color(r, g, b);
                } else {
                    int idx = color_index_from_name(data);
                    if (idx < 0) idx = atoi(data);
                    if (idx >= 0 && idx < color_count) {
                        color_index = idx;
                        custom_color = 0;
                        update_leds();
                    }
                }
            } else if (strcmp(topic, "nightlight/rgb") == 0) {
                uint8_t r, g, b;
                if (parse_rgb(data, &r, &g, &b) == 0) {
                    set_rgb_color(r, g, b);
                }
            } else if (strcmp(topic, "nightlight/brightness") == 0) {
                int b = atoi(data);
                if (b < 0) b = 0;
                if (b > 31) b = 31;
                brightness = (uint8_t)b;
                update_leds();
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

static void mqtt_start(void) {
    if (strlen(g_cfg.mqtt_uri) == 0) return;
    if (g_mqtt) {
        esp_mqtt_client_stop(g_mqtt);
        esp_mqtt_client_destroy(g_mqtt);
        g_mqtt = NULL;
    }
    char uri[192];
    if (strncmp(g_cfg.mqtt_uri, "mqtt://", 7) != 0 &&
        strncmp(g_cfg.mqtt_uri, "mqtts://", 8) != 0) {
        snprintf(uri, sizeof(uri), "mqtt://%s", g_cfg.mqtt_uri);
    } else {
        strncpy(uri, g_cfg.mqtt_uri, sizeof(uri) - 1);
        uri[sizeof(uri) - 1] = '\0';
    }
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = uri,
        .username = g_cfg.mqtt_user,
        .password = g_cfg.mqtt_pass,
    };
    g_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    if (g_mqtt) {
        esp_mqtt_client_register_event(g_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, g_mqtt);
        esp_mqtt_client_start(g_mqtt);
    }
}

static TaskHandle_t s_wifi_reconnect_task = NULL;

static void wifi_reconnect_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (strlen(g_cfg.ssid)) esp_wifi_connect();
    s_wifi_reconnect_task = NULL;
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (strlen(g_cfg.ssid)) esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (strlen(g_cfg.ssid) && s_wifi_reconnect_task == NULL) {
            xTaskCreate(wifi_reconnect_task, "wifi_reconnect", 2048, NULL, 5, &s_wifi_reconnect_task);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        g_ap_clients++;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (g_ap_clients > 0) g_ap_clients--;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: %s", ip4addr_ntoa(&event->ip_info.ip));
        mqtt_start();
    }
}

static void wifi_init(void) {
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    if (g_setup_enabled) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

        wifi_config_t ap_config = {
            .ap = {
                .ssid = "NightLight-Setup",
                .ssid_len = strlen("NightLight-Setup"),
                .password = "",
                .max_connection = 4,
                .authmode = WIFI_AUTH_OPEN
            },
        };
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }

    if (strlen(g_cfg.ssid)) {
        ESP_LOGI(TAG, "STA SSID: %s (len=%d, pass len=%d)", g_cfg.ssid, (int)strlen(g_cfg.ssid), (int)strlen(g_cfg.pass));
        wifi_config_t sta_config;
        memset(&sta_config, 0, sizeof(sta_config));
        strncpy((char *)sta_config.sta.ssid, g_cfg.ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, g_cfg.pass, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        sta_config.sta.threshold.rssi = -127;
        if (strlen(g_cfg.pass)) {
            sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        }
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

static int append_json_str(char *buf, size_t size, int pos, const char *s) {
    if (pos < 0 || (size_t)pos >= size - 1) return pos;
    if ((size_t)pos + 1 < size) buf[pos++] = '"';
    for (const char *p = s; p && *p && (size_t)pos < size - 2; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            if ((size_t)pos + 2 >= size - 1) break;
            buf[pos++] = '\\';
            buf[pos++] = (char)c;
        } else if (c < 0x20) {
        } else {
            buf[pos++] = (char)c;
        }
    }
    if ((size_t)pos < size - 1) buf[pos++] = '"';
    buf[pos] = '\0';
    return pos;
}

static void scan_aps_json(char *buf, size_t size) {
    wifi_scan_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scfg.scan_time.active.min = 100;
    scfg.scan_time.active.max = 300;
    esp_wifi_scan_start(&scfg, true);

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > 40) num = 40;
    wifi_ap_record_t *recs = NULL;
    if (num > 0) recs = calloc(num, sizeof(wifi_ap_record_t));
    uint16_t got = num;
    if (!recs) got = 0;
    if (recs) esp_wifi_scan_get_ap_records(&got, recs);

    char seen[40][33];
    memset(seen, 0, sizeof(seen));
    int seen_count = 0;

    int pos = 0;
    if (size > 0) { buf[0] = '['; pos = 1; buf[1] = '\0'; }
    int first = 1;
    for (int i = 0; i < (int)got; i++) {
        char ssid[33];
        memset(ssid, 0, sizeof(ssid));
        strncpy(ssid, (const char *)recs[i].ssid, 32);
        if (strlen(ssid) == 0) continue;
        int dup = 0;
        for (int s = 0; s < seen_count; s++) {
            if (strcmp(seen[s], ssid) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (seen_count < 40) {
            strncpy(seen[seen_count], ssid, 32);
            seen_count++;
        }
        if (!first) {
            if ((size_t)pos < size - 1) { buf[pos++] = ','; buf[pos] = '\0'; }
        }
        first = 0;
        if ((size_t)pos < size - 1) { buf[pos++] = '{'; buf[pos] = '\0'; }
        if ((size_t)pos + 7 < size) {
            memcpy(buf + pos, "\"ssid\":", 7); pos += 7; buf[pos] = '\0';
        }
        pos = append_json_str(buf, size, pos, ssid);
        if ((size_t)pos + 8 < size) {
            memcpy(buf + pos, ",\"rssi\":", 8); pos += 8; buf[pos] = '\0';
        }
        pos += snprintf(buf + pos, size - pos, "%d", recs[i].rssi);
        if ((size_t)pos < size - 1) { buf[pos++] = '}'; buf[pos] = '\0'; }
    }
    if ((size_t)pos < size - 1) { buf[pos++] = ']'; buf[pos] = '\0'; }
    if (recs) free(recs);
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    g_web_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
    char *buf = malloc(4096);
    if (!buf) return ESP_FAIL;
    scan_aps_json(buf, 4096);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, strlen(buf));
    free(buf);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    g_web_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
    char *buf = malloc(8192);
    if (!buf) return ESP_FAIL;
    char saved_json[128];
    int sp = 0;
    saved_json[0] = '\0';
    sp = append_json_str(saved_json, sizeof(saved_json), sp, g_cfg.ssid);

    snprintf(buf, 8192,
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>NightLight Config</title>"
        "<style>*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;color:#fff}.card{background:rgba(255,255,255,.08);backdrop-filter:blur(10px);border:1px solid rgba(255,255,255,.15);border-radius:16px;padding:28px 32px;max-width:420px;width:100%%;box-shadow:0 10px 40px rgba(0,0,0,.4)}h1{margin:0 0 6px;font-size:24px}.sub{opacity:.7;font-size:14px;margin-bottom:20px}label{display:block;font-size:13px;margin:12px 0 6px;opacity:.9}input,select{width:100%%;padding:10px 12px;border-radius:8px;border:1px solid rgba(255,255,255,.2);background:rgba(0,0,0,.25);color:#fff;font-size:15px}input:focus,select:focus{outline:none;border-color:#4fc3f7;box-shadow:0 0 0 2px rgba(79,195,247,.25)}.hidden{display:none}button{margin-top:20px;width:100%%;padding:12px;border:none;border-radius:8px;background:linear-gradient(90deg,#4fc3f7,#0288d1);color:#fff;font-size:16px;font-weight:600;cursor:pointer}button:hover{filter:brightness(1.1)}.status{margin-top:12px;font-size:13px;opacity:.8;min-height:18px}.spinner{display:inline-block;width:14px;height:14px;border:2px solid rgba(255,255,255,.3);border-top-color:#4fc3f7;border-radius:50%%;animation:spin 1s linear infinite;vertical-align:middle;margin-right:6px}@keyframes spin{to{transform:rotate(360deg)}}</style></head><body><div class='card'><h1>NightLight</h1><p class='sub'>WiFi &amp; MQTT Configuration</p><form id='form' method='POST' action='/save'><label>WiFi Network</label><select id='ssidSelect'><option value='' disabled selected>Scanning...</option></select><input id='ssidCustom' class='hidden' type='text' placeholder='Enter SSID manually'><input type='hidden' name='ssid' id='ssidHidden'><label>WiFi Password</label><input name='password' type='password' value='%s'><label>MQTT URI</label><input name='mqtt_uri' value='%s' placeholder='mqtt://host:1883'><label>MQTT Username</label><input name='mqtt_user' value='%s'><label>MQTT Password</label><input name='mqtt_pass' type='password' value='%s'><button type='submit'>Save &amp; Reboot</button></form><div class='status' id='status'><span class='spinner'></span>Scanning for networks...</div></div><script>const savedSsid=%s;const select=document.getElementById('ssidSelect');const custom=document.getElementById('ssidCustom');const hidden=document.getElementById('ssidHidden');const status=document.getElementById('status');function addOption(v,t,s){const o=document.createElement('option');o.value=v;o.textContent=t;if(s)o.selected=true;select.appendChild(o);}fetch('/scan').then(r=>r.json()).then(list=>{select.innerHTML='';let found=false;list.forEach(ap=>{const sel=savedSsid&&ap.ssid===savedSsid;if(sel)found=true;addOption(ap.ssid,ap.ssid+' ('+ap.rssi+' dBm)',sel);});if(savedSsid&&!found)addOption(savedSsid,savedSsid+' (saved)',true);addOption('__custom__','Other (enter manually)...',false);status.textContent=list.length?('Found '+list.length+' network(s).'):'No networks found.';}).catch(e=>{select.innerHTML='';if(savedSsid)addOption(savedSsid,savedSsid+' (saved)',true);addOption('__custom__','Other (enter manually)...',true);status.textContent='Scan failed. Enter SSID manually.';});select.addEventListener('change',()=>{if(select.value==='__custom__'){custom.classList.remove('hidden');custom.focus();}else custom.classList.add('hidden');});document.getElementById('form').addEventListener('submit',()=>{if(select.value==='__custom__')hidden.value=custom.value;else hidden.value=select.value;});</script></body></html>",
        g_cfg.pass, g_cfg.mqtt_uri, g_cfg.mqtt_user, g_cfg.mqtt_pass, saved_json);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, strlen(buf));
    free(buf);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    g_web_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
    char body[1024];
    int remaining = req->content_len;
    int len = 0;
    while (remaining > 0 && len < (int)sizeof(body) - 1) {
        int to_read = remaining;
        if (to_read > (int)sizeof(body) - 1 - len) to_read = (int)sizeof(body) - 1 - len;
        int ret = httpd_req_recv(req, body + len, to_read);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return ESP_FAIL;
        }
        len += ret;
        remaining -= ret;
    }
    body[len] = '\0';

    app_config_t cfg = g_cfg;
    get_field(body, "ssid", cfg.ssid, sizeof(cfg.ssid));
    get_field(body, "password", cfg.pass, sizeof(cfg.pass));
    get_field(body, "mqtt_uri", cfg.mqtt_uri, sizeof(cfg.mqtt_uri));
    get_field(body, "mqtt_user", cfg.mqtt_user, sizeof(cfg.mqtt_user));
    get_field(body, "mqtt_pass", cfg.mqtt_pass, sizeof(cfg.mqtt_pass));

    save_config(&cfg);

    const char *resp = "Saved. Rebooting...";
    httpd_resp_send(req, resp, strlen(resp));

    g_web_until = 0;
    if (g_server) {
        httpd_stop(g_server);
        g_server = NULL;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);

    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
        httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler, .user_ctx = NULL };
        httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &scan);
        httpd_register_uri_handler(server, &save);
        return server;
    }
    return NULL;
}

static void enable_setup_mode(void) {
    if (g_setup_enabled) {
        if (!g_server) g_server = start_webserver();
        g_web_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
        return;
    }
    g_setup_enabled = 1;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "setup_enabled", 1);
        nvs_commit(h);
        nvs_close(h);
    }
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "NightLight-Setup",
            .ssid_len = strlen("NightLight-Setup"),
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config);
    if (!g_server) g_server = start_webserver();
    g_web_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
}

static void buttons_task(void *pvParameters) {
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BTN_COLOR_GPIO) |
                        (1ULL << BTN_BRIGHT_UP_GPIO) |
                        (1ULL << BTN_BRIGHT_DOWN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_conf);

    int last_color = 1;
    int last_up = 1;
    int last_down = 1;
    TickType_t both_since = 0;
    int both_tracking = 0;

    while (1) {
        int color = gpio_get_level(BTN_COLOR_GPIO);
        int up = gpio_get_level(BTN_BRIGHT_UP_GPIO);
        int down = gpio_get_level(BTN_BRIGHT_DOWN_GPIO);

        if (last_color == 1 && color == 0) {
            color_index = (color_index + 1) % color_count;
            custom_color = 0;
            update_leds();
        }

        if (last_up == 1 && up == 0 && down == 1) {
            if (brightness + 4 > 31) brightness = 31; else brightness += 4;
            update_leds();
        }

        if (last_down == 1 && down == 0 && up == 1) {
            if (brightness < 4) brightness = 0; else brightness -= 4;
            update_leds();
        }

        if (up == 0 && down == 0) {
            if (!both_tracking) {
                both_tracking = 1;
                both_since = xTaskGetTickCount();
            } else if (!g_setup_enabled &&
                       (xTaskGetTickCount() - both_since) >= pdMS_TO_TICKS(5000)) {
                enable_setup_mode();
                both_tracking = 0;
            }
        } else {
            both_tracking = 0;
        }

        last_color = color;
        last_up = up;
        last_down = down;

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void led_blink_task(void *pvParameters) {
    int was_active = 0;
    int on = 1;
    while (1) {
        if (setup_mode_active()) {
            if (!was_active) {
                was_active = 1;
                on = 1;
            }
            if (on) {
                sk9822_set_color(255, 0, 0, brightness ? brightness : 16);
            } else {
                sk9822_set_color(0, 0, 0, 0);
            }
            on = !on;
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            if (was_active) {
                was_active = 0;
                update_leds();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void) {
    sk9822_init();
    update_leds();

    nvs_init_safe();
    load_config();

    xTaskCreate(buttons_task, "buttons_task", 2048, NULL, 5, NULL);
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);

    wifi_init();

    if (g_setup_enabled) {
        g_server = start_webserver();
    }
}
