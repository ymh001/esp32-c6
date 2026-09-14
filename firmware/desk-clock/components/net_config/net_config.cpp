#include "net_config.h"

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "board.h"
#include "dhcpserver/dhcpserver.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "time_service.h"

static const char *TAG = "network";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define MAX_SCAN_RESULTS 16
#define PORTAL_BODY_MAX 4096

typedef struct {
    const char *label;
    const char *posix_tz;
} timezone_option_t;

static const timezone_option_t s_timezones[] = {
    {"China (UTC+8)", "CST-8"},
    {"Hong Kong (UTC+8)", "HKT-8"},
    {"Taiwan (UTC+8)", "CST-8"},
    {"Japan (UTC+9)", "JST-9"},
    {"Korea (UTC+9)", "KST-9"},
    {"UTC", "UTC0"},
    {"UK (London)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"US Eastern", "EST5EDT,M3.2.0/2,M11.1.0/2"},
    {"US Pacific", "PST8PDT,M3.2.0/2,M11.1.0/2"},
    {"Australia Sydney", "AEST-10AEDT,M10.1.0/2,M4.1.0/3"},
};

static clock_settings_t s_settings;
static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_http_server;
static TaskHandle_t s_dns_task;
static volatile net_state_t s_state = NET_STATE_IDLE;
static volatile int s_retry_count;
static volatile bool s_station_connecting;
static bool s_started;
static char s_ip[16] = "0.0.0.0";
static wifi_ap_record_t s_scan_results[MAX_SCAN_RESULTS];
static uint16_t s_scan_count;
static volatile bool s_scan_in_progress;
static SemaphoreHandle_t s_scan_mutex;

static void set_state(net_state_t state)
{
    s_state = state;
}

static void append_text(char *buffer, size_t capacity, size_t *offset,
                        const char *format, ...)
{
    if (*offset >= capacity) {
        return;
    }
    va_list args;
    va_start(args, format);
    const int written =
        vsnprintf(buffer + *offset, capacity - *offset, format, args);
    va_end(args);
    if (written > 0) {
        *offset += (size_t)written;
        if (*offset >= capacity) {
            *offset = capacity - 1;
        }
    }
}

static void html_escape(const char *input, char *output, size_t size)
{
    size_t out = 0;
    if (size == 0) {
        return;
    }
    for (const char *p = input; *p != '\0' && out + 1 < size; ++p) {
        const char *replacement = NULL;
        switch (*p) {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&#39;";
            break;
        default:
            break;
        }
        if (replacement != NULL) {
            const size_t length = strlen(replacement);
            if (out + length >= size) {
                break;
            }
            memcpy(output + out, replacement, length);
            out += length;
        } else {
            output[out++] = *p;
        }
    }
    output[out] = '\0';
}

static void url_decode_in_place(char *value)
{
    char *read = value;
    char *write = value;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            ++read;
        } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            const char hex[3] = {read[1], read[2], '\0'};
            *write++ = (char)strtol(hex, NULL, 16);
            read += 3;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static bool form_get_value(char *body, const char *key, char *out,
                           size_t out_size)
{
    if (out_size == 0) {
        return false;
    }
    out[0] = '\0';

    char *cursor = body;
    while (cursor != NULL && *cursor != '\0') {
        char *next = strchr(cursor, '&');
        if (next != NULL) {
            *next++ = '\0';
        }

        char *equals = strchr(cursor, '=');
        if (equals != NULL) {
            *equals = '\0';
            url_decode_in_place(cursor);
            url_decode_in_place(equals + 1);
            if (strcmp(cursor, key) == 0) {
                strlcpy(out, equals + 1, out_size);
                return true;
            }
        }
        cursor = next;
    }
    return false;
}

static void append_timezone_options(char *html, size_t capacity,
                                    size_t *offset)
{
    for (size_t i = 0; i < sizeof(s_timezones) / sizeof(s_timezones[0]); ++i) {
        append_text(html, capacity, offset, "<option value=\"%s\"%s>%s</option>",
                    s_timezones[i].posix_tz,
                    strcmp(s_settings.timezone, s_timezones[i].posix_tz) == 0
                        ? " selected"
                        : "",
                    s_timezones[i].label);
    }
}

static void append_wifi_options(char *html, size_t capacity, size_t *offset)
{
    if (!net_config_is_portal()) {
        return;
    }

    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    if (s_scan_count == 0) {
        append_text(html, capacity, offset, "<option value=\"\">%s</option>",
                    s_scan_in_progress ? "Scanning..." : "No networks found");
    } else {
        char escaped[128];
        for (uint16_t i = 0; i < s_scan_count; ++i) {
            if (s_scan_results[i].ssid[0] == '\0') {
                continue;
            }
            html_escape((const char *)s_scan_results[i].ssid, escaped,
                        sizeof(escaped));
            append_text(html, capacity, offset,
                        "<option value=\"%s\"%s>%s (%d dBm)</option>",
                        escaped,
                        strcmp((const char *)s_scan_results[i].ssid,
                               s_settings.wifi_ssid) == 0
                            ? " selected"
                            : "",
                        escaped, s_scan_results[i].rssi);
        }
    }
    xSemaphoreGive(s_scan_mutex);
}

static void portal_scan_task(void *arg)
{
    (void)arg;
    s_scan_in_progress = true;
    vTaskDelay(pdMS_TO_TICKS(300));

    wifi_scan_config_t scan = {};
    scan.show_hidden = false;
    const esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err == ESP_OK) {
        uint16_t count = MAX_SCAN_RESULTS;
        wifi_ap_record_t records[MAX_SCAN_RESULTS] = {};
        if (esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
            xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
            memcpy(s_scan_results, records, count * sizeof(records[0]));
            s_scan_count = count;
            xSemaphoreGive(s_scan_mutex);
        }
    } else {
        ESP_LOGW(TAG, "Portal Wi-Fi scan failed: %s", esp_err_to_name(err));
    }

    s_scan_in_progress = false;
    vTaskDelete(NULL);
}

static void start_portal_scan(void)
{
    if (s_scan_in_progress) {
        return;
    }
    if (xTaskCreate(portal_scan_task, "portal_scan", 4096, NULL, 4, NULL) !=
        pdPASS) {
        ESP_LOGW(TAG, "Unable to create portal scan task");
    }
}

static char *build_portal_page(void)
{
    const size_t capacity = 16384;
    char *html = (char *)calloc(1, capacity);
    if (html == NULL) {
        return NULL;
    }

    size_t offset = 0;
    char ssid[128];
    html_escape(s_settings.wifi_ssid, ssid, sizeof(ssid));
    append_text(html, capacity, &offset,
                "<!doctype html><html><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>Desk Clock Setup</title><style>"
                "body{margin:0;background:#0b0e13;color:#edf1f7;font:16px system-ui,sans-serif}"
                "main{max-width:620px;margin:0 auto;padding:28px 20px 48px}"
                "h1{font-size:30px;margin:0 0 8px}p{color:#9ba5b5;margin:0 0 24px}"
                "form{display:grid;gap:16px}.field{display:grid;gap:7px}"
                "label{font-weight:650}input,select{box-sizing:border-box;width:100%%;"
                "padding:12px;border-radius:8px;border:1px solid #3a4352;"
                "background:#151a22;color:#fff;font:inherit}"
                "input[type=checkbox]{width:auto}.row{display:grid;grid-template-columns:"
                "1fr 1fr;gap:14px}.hint{font-size:13px;color:#8792a3}"
                "button{margin-top:8px;padding:13px 18px;border:0;border-radius:8px;"
                "background:#f3a712;color:#17120a;font-weight:800;font-size:17px}"
                ".status{padding:12px 14px;border-radius:8px;background:#151a22;"
                "border:1px solid #2a3340;margin-bottom:20px}"
                "</style></head><body><main><h1>Desk Clock</h1>"
                "<p>Wi-Fi, time zone and clock preferences</p>"
                "<div class=\"status\">Network: %s<br>IP: %s</div>"
                "<form method=\"post\" action=\"/save\">"
                "<div class=\"field\"><label for=\"ssid_select\">Wi-Fi network</label>"
                "<select id=\"ssid_select\" name=\"ssid_select\">"
                "<option value=\"\">Select a scanned network</option>",
                net_config_is_portal() ? "configuration hotspot"
                                       : "connected",
                s_ip);

    append_wifi_options(html, capacity, &offset);
    append_text(html, capacity, &offset,
                "</select>"
                "<input id=\"ssid_manual\" name=\"ssid_manual\" "
                "placeholder=\"Or enter SSID manually\" value=\"%s\"></div>"
                "<div class=\"field\"><label for=\"password\">Wi-Fi password</label>"
                "<input id=\"password\" name=\"password\" type=\"password\" "
                "autocomplete=\"current-password\"></div>"
                "<div class=\"row\"><div class=\"field\">"
                "<label for=\"timezone\">Time zone</label><select id=\"timezone\" "
                "name=\"timezone\">",
                ssid);
    append_timezone_options(html, capacity, &offset);
    append_text(html, capacity, &offset,
                "</select></div><div class=\"field\">"
                "<label for=\"brightness\">Brightness (%%):</label>"
                "<input id=\"brightness\" name=\"brightness\" type=\"number\" "
                "min=\"10\" max=\"100\" value=\"%u\"></div></div>"
                "<div class=\"row\"><div class=\"field\">"
                "<label for=\"manual_time\">Set time manually (optional)</label>"
                "<input id=\"manual_time\" name=\"manual_time\" "
                "type=\"datetime-local\"></div><div class=\"field\">"
                "<label>Clock format</label><label class=\"hint\">"
                "<input name=\"use24\" type=\"checkbox\" value=\"1\"%s> "
                "Use 24-hour time</label></div></div>"
                "<button type=\"submit\">Save and restart</button></form>"
                "<p class=\"hint\">Open http://192.168.4.1 while using the setup "
                "hotspot. The saved Wi-Fi password and local settings are stored "
                "only in device NVS.</p><script>"
                "(function(){var s=document.getElementById('ssid_select');"
                "var n=0;function r(){fetch('/scan',{cache:'no-store'}).then("
                "function(x){return x.text()}).then(function(t){"
                "if(t.indexOf('Scanning...')<0){s.innerHTML=t;return}"
                "if(n++<10)setTimeout(r,1000)}).catch(function(){"
                "if(n++<10)setTimeout(r,1000)})}"
                "setTimeout(r,1000)})();</script></main></body></html>",
                s_settings.brightness,
                s_settings.use_24_hour ? " checked" : "");

    return html;
}

static esp_err_t root_handler(httpd_req_t *request)
{
    char *html = build_portal_page();
    if (html == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Out of memory");
    }
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_send(request, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return err;
}

static void apply_manual_time(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return;
    }
    int year, month, day, hour, minute;
    if (sscanf(value, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) !=
        5) {
        return;
    }

    struct tm local = {};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_isdst = -1;
    const time_t epoch = mktime(&local);
    if (epoch <= 0) {
        return;
    }
    const struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, NULL);
    board_rtc_write(&local);
}

static esp_err_t save_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len >= PORTAL_BODY_MAX) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid form size");
    }

    char *body = (char *)calloc(1, PORTAL_BODY_MAX);
    if (body == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Out of memory");
    }

    int received = 0;
    while (received < request->content_len) {
        const int result = httpd_req_recv(
            request, body + received, request->content_len - received);
        if (result <= 0) {
            free(body);
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                       "Receive failed");
        }
        received += result;
    }
    body[received] = '\0';

    char value[CLOCK_TIMEZONE_MAX];
    clock_settings_t next = s_settings;

    char manual_ssid[CLOCK_WIFI_SSID_MAX] = {0};
    if (form_get_value(body, "ssid_manual", manual_ssid,
                       sizeof(manual_ssid)) &&
        manual_ssid[0] != '\0') {
        strlcpy(next.wifi_ssid, manual_ssid, sizeof(next.wifi_ssid));
    } else if (form_get_value(body, "ssid_select", value, sizeof(value)) &&
               value[0] != '\0') {
        strlcpy(next.wifi_ssid, value, sizeof(next.wifi_ssid));
    } else if (form_get_value(body, "ssid", value, sizeof(value))) {
        strlcpy(next.wifi_ssid, value, sizeof(next.wifi_ssid));
    }
    if (form_get_value(body, "password", value, sizeof(value))) {
        strlcpy(next.wifi_password, value, sizeof(next.wifi_password));
    }
    if (form_get_value(body, "timezone", value, sizeof(value))) {
        strlcpy(next.timezone, value, sizeof(next.timezone));
    }
    if (form_get_value(body, "brightness", value, sizeof(value))) {
        const int brightness = atoi(value);
        if (brightness >= 10 && brightness <= 100) {
            next.brightness = (uint8_t)brightness;
        }
    }
    next.use_24_hour =
        form_get_value(body, "use24", value, sizeof(value)) ? 1 : 0;
    next.force_portal = false;

    const esp_err_t err = clock_settings_save(&next);
    if (err != ESP_OK) {
        free(body);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Failed to save settings");
    }

    s_settings = next;
    time_service_apply_timezone(s_settings.timezone);
    board_set_backlight(s_settings.brightness);
    if (form_get_value(body, "manual_time", value, sizeof(value))) {
        apply_manual_time(value);
    }
    free(body);

    ESP_LOGI(TAG, "Saved configuration for Wi-Fi SSID '%s'",
             s_settings.wifi_ssid);

    httpd_resp_set_type(request, "text/html");
    const esp_err_t response = httpd_resp_sendstr(
        request,
        "<!doctype html><meta name=\"viewport\" content=\"width=device-width\">"
        "<body style=\"background:#0b0e13;color:#edf1f7;font:18px system-ui;"
        "padding:32px\"><h2>Settings saved</h2><p>The clock is restarting.</p>"
        "</body>");

    esp_timer_handle_t restart_timer = NULL;
    const esp_timer_create_args_t timer_args = {
        .callback = [](void *) { esp_restart(); },
        .name = "settings_restart",
    };
    if (esp_timer_create(&timer_args, &restart_timer) == ESP_OK) {
        esp_timer_start_once(restart_timer, 1500000);
    }
    return response;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    char response[192];
    snprintf(response, sizeof(response),
             "{\"network\":\"%s\",\"ip\":\"%s\"}",
             s_state == NET_STATE_CONNECTED ? "connected" : "portal", s_ip);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t scan_handler(httpd_req_t *request)
{
    const size_t capacity = 8192;
    char *html = (char *)calloc(1, capacity);
    if (html == NULL) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Out of memory");
    }

    size_t offset = 0;
    append_wifi_options(html, capacity, &offset);
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t err = httpd_resp_sendstr(request, html);
    free(html);
    return err;
}

static esp_err_t portal_handler(httpd_req_t *request)
{
    clock_settings_set_portal_request(true);
    httpd_resp_set_type(request, "text/html");
    const esp_err_t err = httpd_resp_sendstr(
        request,
        "<!doctype html><body style=\"background:#0b0e13;color:#edf1f7;"
        "font:18px system-ui;padding:32px\"><h2>Setup mode</h2>"
        "<p>Restarting into the configuration hotspot.</p></body>");
    esp_timer_handle_t restart_timer = NULL;
    const esp_timer_create_args_t timer_args = {
        .callback = [](void *) { esp_restart(); },
        .name = "portal_restart",
    };
    if (esp_timer_create(&timer_args, &restart_timer) == ESP_OK) {
        esp_timer_start_once(restart_timer, 1000000);
    }
    return err;
}

static esp_err_t captive_redirect_handler(httpd_req_t *request)
{
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, "Redirecting to Desk Clock setup");
}

static void dns_server_task(void *arg)
{
    (void)arg;
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "DNS socket failed: errno %d", errno);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(53);
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed: errno %d", errno);
        close(socket_fd);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint8_t query[512];
    uint8_t response[512];
    for (;;) {
        struct sockaddr_in source = {};
        socklen_t source_length = sizeof(source);
        const int length =
            recvfrom(socket_fd, query, sizeof(query), 0,
                     (struct sockaddr *)&source, &source_length);
        if (length < 17 || (query[2] & 0x80) != 0 || query[4] != 0 ||
            query[5] == 0) {
            continue;
        }

        size_t question_end = 12;
        while (question_end < (size_t)length && query[question_end] != 0) {
            question_end += query[question_end] + 1;
        }
        question_end += 5;
        if (question_end > (size_t)length || question_end + 16 > sizeof(response)) {
            continue;
        }

        memcpy(response, query, question_end);
        response[2] = 0x81;
        response[3] = 0x80;
        response[6] = 0;
        response[7] = 1;
        response[8] = 0;
        response[9] = 0;
        response[10] = 0;
        response[11] = 0;

        uint8_t *answer = response + question_end;
        answer[0] = 0xC0;
        answer[1] = 0x0C;
        answer[2] = 0;
        answer[3] = 1;
        answer[4] = 0;
        answer[5] = 1;
        answer[6] = 0;
        answer[7] = 0;
        answer[8] = 0;
        answer[9] = 60;
        answer[10] = 0;
        answer[11] = 4;
        answer[12] = 192;
        answer[13] = 168;
        answer[14] = 4;
        answer[15] = 1;

        sendto(socket_fd, response, question_end + 16, 0,
               (struct sockaddr *)&source, source_length);
    }
}

static void start_dns_server(void)
{
    if (s_dns_task != NULL) {
        return;
    }
    xTaskCreate(dns_server_task, "captive_dns", 4096, NULL, 4, &s_dns_task);
}

static esp_err_t start_web_server(void)
{
    if (s_http_server != NULL) {
        return ESP_OK;
    }

    const httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_config_t actual = config;
    actual.max_uri_handlers = 12;
    actual.stack_size = 8192;
    actual.max_open_sockets = 5;
    actual.backlog_conn = 4;
    actual.recv_wait_timeout = 3;
    actual.send_wait_timeout = 5;
    actual.lru_purge_enable = true;
    actual.keep_alive_enable = false;
    ESP_RETURN_ON_ERROR(httpd_start(&s_http_server, &actual), TAG,
                        "HTTP server start failed");

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    const httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
    };
    const httpd_uri_t status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
    };
    const httpd_uri_t portal = {
        .uri = "/portal",
        .method = HTTP_POST,
        .handler = portal_handler,
    };
    const httpd_uri_t scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_handler,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &root), TAG,
                        "Register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &save), TAG,
                        "Register save failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &status),
                        TAG, "Register status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &portal),
                        TAG, "Register portal failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_http_server, &scan), TAG,
                        "Register scan failed");

    if (net_config_is_portal()) {
        const httpd_uri_t redirects[] = {
            {.uri = "/generate_204", .method = HTTP_GET,
             .handler = captive_redirect_handler},
            {.uri = "/hotspot-detect.html", .method = HTTP_GET,
             .handler = captive_redirect_handler},
            {.uri = "/library/test/success.html", .method = HTTP_GET,
             .handler = captive_redirect_handler},
            {.uri = "/connecttest.txt", .method = HTTP_GET,
             .handler = captive_redirect_handler},
            {.uri = "/ncsi.txt", .method = HTTP_GET,
             .handler = captive_redirect_handler},
        };
        for (size_t i = 0; i < sizeof(redirects) / sizeof(redirects[0]); ++i) {
            ESP_RETURN_ON_ERROR(
                httpd_register_uri_handler(s_http_server, &redirects[i]), TAG,
                "Register captive redirect failed");
        }
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_station_connecting) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Station disconnected (reason %u, retry %d/3)",
                 event->reason, s_retry_count + 1);
        if (s_station_connecting && s_retry_count < 3) {
            ++s_retry_count;
            set_state(NET_STATE_CONNECTING);
            esp_wifi_connect();
        } else if (s_station_connecting) {
            s_station_connecting = false;
            set_state(NET_STATE_ERROR);
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_station_connecting = false;
        set_state(NET_STATE_CONNECTED);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected, IP: %s", s_ip);
    }
}

static esp_err_t initialize_wifi(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "event loop init");
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        return ESP_FAIL;
    }
    esp_netif_set_hostname(s_sta_netif, "desk-clock");

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   wifi_event_handler, NULL),
        TAG, "Wi-Fi event registration");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   wifi_event_handler, NULL),
        TAG, "IP event registration");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "Wi-Fi storage");
    return ESP_OK;
}

static esp_err_t connect_station(void)
{
    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, s_settings.wifi_ssid,
            sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, s_settings.wifi_password,
            sizeof(config.sta.password));
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    set_state(NET_STATE_CONNECTING);
    s_station_connecting = true;
    s_retry_count = 0;
    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID '%s'", s_settings.wifi_ssid);
    xEventGroupClearBits(s_wifi_events,
                         WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "Set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG,
                        "Set STA config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Start STA");

    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT, pdFALSE, pdFALSE,
        pdMS_TO_TICKS(20000));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        clock_settings_set_portal_request(false);
        return start_web_server();
    }
    s_station_connecting = false;
    return ESP_FAIL;
}

static esp_err_t start_portal(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "DeskClock-%02X%02X", mac[4], mac[5]);

    wifi_config_t actual = {};
    strlcpy((char *)actual.ap.ssid, ssid, sizeof(actual.ap.ssid));
    actual.ap.ssid_len = strlen(ssid);
    actual.ap.channel = 1;
    actual.ap.max_connection = 4;
    actual.ap.authmode = WIFI_AUTH_OPEN;
    actual.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG,
                        "Set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &actual), TAG,
                        "Set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Start AP");
    snprintf(s_ip, sizeof(s_ip), "192.168.4.1");
    set_state(NET_STATE_PORTAL);
    ESP_LOGI(TAG, "Configuration hotspot: %s", ssid);
    start_dns_server();
    start_portal_scan();

    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    IP4_ADDR(&dns.ip.u_addr.ip4, 192, 168, 4, 1);
    const uint8_t offer_dns = OFFER_DNS;
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                           ESP_NETIF_DOMAIN_NAME_SERVER, (void *)&offer_dns,
                           sizeof(offer_dns));
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(s_ap_netif);
    return start_web_server();
}

static void network_task(void *arg)
{
    if (s_settings.force_portal || s_settings.wifi_ssid[0] == '\0') {
        if (start_portal() != ESP_OK) {
            set_state(NET_STATE_ERROR);
        }
        vTaskDelete(NULL);
        return;
    }

    if (connect_station() == ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "Station connection failed; opening setup hotspot");
    esp_wifi_stop();
    if (start_portal() != ESP_OK) {
        set_state(NET_STATE_ERROR);
    }
    vTaskDelete(NULL);
}

esp_err_t net_config_start(const clock_settings_t *settings)
{
    if (settings == NULL || s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_started = true;
    s_settings = *settings;
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_scan_mutex = xSemaphoreCreateMutex();
    if (s_scan_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(initialize_wifi(), TAG, "Wi-Fi setup failed");
    if (xTaskCreate(network_task, "network", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

net_state_t net_config_state(void)
{
    return s_state;
}

bool net_config_is_connected(void)
{
    return s_state == NET_STATE_CONNECTED;
}

bool net_config_is_portal(void)
{
    return s_state == NET_STATE_PORTAL;
}

void net_config_get_ip(char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    strlcpy(out, s_ip, size);
}

void net_config_request_portal(void)
{
    clock_settings_set_portal_request(true);
    esp_restart();
}
