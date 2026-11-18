/* espnow_link.cpp — реализация ESP-NOW. ARCHITECTURE §2.
 * Единственный файл, включающий esp_now.h / WiFi.h.
 *
 * API соответствует Arduino-ядру 3.x (ESP-IDF 5.x): колбэки ESP-NOW
 * принимают структуры esp_now_recv_info_t / esp_now_send_info_t.
 */
#include "espnow_link.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <string.h>

static EspNowRecvCb g_on_recv = nullptr;
static EspNowSendCb g_on_send = nullptr;
static uint8_t g_self_mac[6] = {0};

/* Приём: RSSI берём из rx_ctrl — это поле битовое (signed rssi: 8) */
static void recv_cb(const esp_now_recv_info_t* info,
                    const uint8_t* data, int len) {
    if (!g_on_recv || !data || len <= 0) return;

    int8_t rssi = 0;
    const uint8_t* src = nullptr;
    if (info) {
        if (info->rx_ctrl) rssi = (int8_t)info->rx_ctrl->rssi;
        src = info->src_addr;
    }
    g_on_recv(data, (size_t)len, src ? src : (const uint8_t*)"\xFF\xFF\xFF\xFF\xFF\xFF", rssi);
}

/* Отправка: mac недоступен в esp_now_send_info_t — отдаём свой */
static void send_cb(const esp_now_send_info_t* tx_info,
                    esp_now_send_status_t status) {
    (void)tx_info;
    if (g_on_send) g_on_send(g_self_mac, status == ESP_NOW_SEND_SUCCESS ? 0 : 1);
}

bool espnow_link_init(EspNowRecvCb on_recv, EspNowSendCb on_send) {
    g_on_recv = on_recv;
    g_on_send = on_send;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) return false;

    esp_now_register_recv_cb(recv_cb);
    esp_now_register_send_cb(send_cb);

    /* Broadcast peer — обязателен для широковещания */
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    /* Свой MAC */
    esp_read_mac(g_self_mac, ESP_MAC_WIFI_STA);
    return true;
}

void espnow_link_set_self(const uint8_t mac[6]) {
    if (mac) memcpy(g_self_mac, mac, 6);
}

bool espnow_link_send(const uint8_t* mac, const uint8_t* data, size_t len) {
    if (!mac || !data || len == 0 || len > 250) return false;
    return esp_now_send(mac, data, len) == ESP_OK;
}

bool espnow_link_send_broadcast(const uint8_t* data, size_t len) {
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    return espnow_link_send(bcast, data, len);
}

void espnow_link_add_peer(const uint8_t mac[6]) {
    if (!mac) return;
    if (esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

void espnow_link_deinit() {
    esp_now_deinit();
    g_on_recv = nullptr;
    g_on_send = nullptr;
}
