/* ble_gateway.cpp — реализация BLE-шлюза. BLE_API §2–4.
 * Использует NimBLE (BLE_API §2: меньше RAM, критично на C3).
 *
 * Кадрирование: [SOF(1) | len(2 LE) | payload(len) | crc16(2 LE)].
 * payload — JSON (BLE_API §4, решение §6.1: JSON в v1).
 */
#include "ble_gateway.h"
#include "config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

/* ---- CRC-16/CCITT (poly 0x1021, init 0xFFFF) — как в web/js/protocol.js ---- */
static uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static NimBLEServer*        g_server = nullptr;
static NimBLECharacteristic* g_rx = nullptr;   /* клиент → шлюз */
static NimBLECharacteristic* g_tx = nullptr;   /* шлюз → клиент */
static NimBLEAdvertising*    g_adv = nullptr;

static BleCmdCb   g_on_cmd = nullptr;
static BleEventCb g_on_event = nullptr;
static bool       g_client_connected = false;

/* ---- Разбор JSON без тяжёлой библиотеки: минимальный парсер ----
 * BLE_API §4: клиент шлёт простые объекты с фиксированными полями.
 * Полноценный JSON-парсер — избыточен для C3. Разбираем вручную. */

static bool jsonGetString(const char* json, const char* key, char* out, size_t cap) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) {
        if (*p == '\\' && p[1]) p++;   /* простой escape */
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}

static bool jsonGetUInt(const char* json, const char* key, uint32_t* out) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    char* end = nullptr;
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) return false;
    *out = (uint32_t)v;
    return true;
}

static bool jsonGetBool(const char* json, const char* key, bool* out) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

/* ---- Приём: буферизация кадра, поиск SOF, сборка по len ---- */
static uint8_t  g_frame_buf[MESH_FRAME_MAX];
static size_t   g_frame_len = 0;

static void processCompleteFrame(const uint8_t* payload, size_t len) {
    /* payload — JSON */
    char json[MESH_FRAME_MAX + 1];
    memcpy(json, payload, len);
    json[len] = 0;

    BleCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = BLE_CMD_NONE;

    char type[16] = {0};
    if (!jsonGetString(json, "t", type, sizeof(type))) return;

    if (strcmp(type, "send") == 0) {
        cmd.type = BLE_CMD_SEND;
        uint32_t v = 0;
        if (jsonGetUInt(json, "dst", &v)) cmd.dst = (uint16_t)v;
        jsonGetString(json, "text", cmd.text, sizeof(cmd.text));
        cmd.ack = true;
        jsonGetBool(json, "ack", &cmd.ack);
    } else if (strcmp(type, "nodes") == 0) {
        cmd.type = BLE_CMD_NODES;
    } else if (strcmp(type, "ping") == 0) {
        cmd.type = BLE_CMD_PING;
        uint32_t v = 0;
        jsonGetUInt(json, "id", &v);
        cmd.ping_id = v;
    } else if (strcmp(type, "get_name") == 0) {
        cmd.type = BLE_CMD_GET_NAME;
    } else if (strcmp(type, "set_name") == 0) {
        cmd.type = BLE_CMD_SET_NAME;
        jsonGetString(json, "name", cmd.name, sizeof(cmd.name));
    } else if (strcmp(type, "clear_name") == 0) {
        cmd.type = BLE_CMD_CLEAR_NAME;
    } else {
        return;
    }

    if (g_on_cmd) g_on_cmd(cmd);
}

/* Разбор потоковых байтов: ищем SOF, собираем по len */
static void feedBytes(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        if (g_frame_len == 0) {
            if (b == MESH_FRAME_SOF) {
                g_frame_buf[g_frame_len++] = b;
            }
            continue;
        }

        g_frame_buf[g_frame_len++] = b;

        if (g_frame_len == 3) {
            /* Заголовок получен: проверяем длину */
            size_t plen = (size_t)g_frame_buf[1] | ((size_t)g_frame_buf[2] << 8);
            if (plen == 0 || plen > MESH_FRAME_MAX - 5) {
                g_frame_len = 0;   /* отбрасываем мусор */
                continue;
            }
        }

        if (g_frame_len >= 3) {
            size_t plen = (size_t)g_frame_buf[1] | ((size_t)g_frame_buf[2] << 8);
            size_t total = 3 + plen + 2;
            if (g_frame_len == total) {
                /* Проверка CRC */
                uint16_t got = (uint16_t)g_frame_buf[3 + plen] |
                               ((uint16_t)g_frame_buf[4 + plen] << 8);
                uint16_t calc = crc16(&g_frame_buf[3], plen);
                if (got == calc) {
                    processCompleteFrame(&g_frame_buf[3], plen);
                }
                g_frame_len = 0;
            } else if (g_frame_len > total) {
                g_frame_len = 0;
            }
        }

        if (g_frame_len >= MESH_FRAME_MAX) g_frame_len = 0;
    }
}

/* ---- Колбэки NimBLE ---- */
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        g_client_connected = true;
        Serial.println("[BLE] client connected");
    }
    void onDisconnect(NimBLEServer* pServer) override {
        g_client_connected = false;
        Serial.println("[BLE] client disconnected");
        /* Возобновляем рекламу для следующего подключения */
        if (g_adv) g_adv->start();
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr) override {
        NimBLEAttValue v = chr->getValue();
        if (v.size() > 0) feedBytes(v.data(), v.size());
    }
};

static ServerCallbacks g_server_cb;
static RxCallbacks     g_rx_cb;

bool ble_gateway_init(const char* device_name, BleCmdCb on_cmd, BleEventCb on_event) {
    g_on_cmd = on_cmd;
    g_on_event = on_event;

    NimBLEDevice::init(device_name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(&g_server_cb);

    NimBLEService* service = g_server->createService(MESH_SERVICE_UUID);

    g_rx = service->createCharacteristic(
        MESH_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    g_rx->setCallbacks(&g_rx_cb);

    g_tx = service->createCharacteristic(
        MESH_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

    service->start();

    g_adv = NimBLEDevice::getAdvertising();
    g_adv->addServiceUUID(MESH_SERVICE_UUID);
    g_adv->setScanResponse(true);
    g_adv->start();

    Serial.println("[BLE] gateway started as " + String(device_name));
    return true;
}

bool ble_gateway_connected() {
    return g_client_connected;
}

void ble_gateway_send_frame(const uint8_t* payload, size_t len) {
    if (!g_tx || !g_client_connected) return;
    if (len + 5 > MESH_FRAME_MAX) return;

    uint8_t frame[MESH_FRAME_MAX];
    size_t off = 0;
    frame[off++] = MESH_FRAME_SOF;
    frame[off++] = (uint8_t)(len & 0xff);
    frame[off++] = (uint8_t)((len >> 8) & 0xff);
    memcpy(&frame[off], payload, len);
    off += len;
    uint16_t crc = crc16(payload, len);
    frame[off++] = (uint8_t)(crc & 0xff);
    frame[off++] = (uint8_t)((crc >> 8) & 0xff);

    g_tx->setValue(frame, off);
    g_tx->notify();
}

void ble_gateway_send_json(const char* json) {
    if (!json) return;
    ble_gateway_send_frame((const uint8_t*)json, strlen(json));
}

void ble_gateway_poll() {
    /* NimBLE работает в своих задачах; здесь — ничего.
     * Оставлено как точка расширения (тайм-ауты, переодическая реклама).
     */
}

void ble_gateway_disconnect_client() {
    if (g_server && g_client_connected) {
        g_server->disconnect(0);
    }
}
