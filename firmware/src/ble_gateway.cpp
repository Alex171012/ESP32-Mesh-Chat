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

/* ---- Межзадачный обмен nimble_host → loopTask ----
 * Коллбэки NimBLE (onWrite/onMTUChange/onSubscribe) выполняются в задаче
 * nimble_host со стеком ~5 КБ. Тяжёлая работа — сборка JSON и кадра
 * (буферы до 4 КБ!) и notify() — раньше выполнялась прямо в коллбэке и
 * переполняла стек хоста: «Guru Meditation Error: Core 0 panic'ed (Stack
 * protection fault). Detected in task "nimble_host"» на первом коннекте.
 * Теперь коллбэки только складывают данные, а весь труд делает
 * ble_gateway_poll(), которую main.cpp вызывает из loopTask (стек 8 КБ).
 * C3 одноядерный: volatile-переменные между задачами безопасны. */

/* MTU пира: 23 до обмена; обновляется в onMTUChange/onConnect,
 * сбрасывается в onDisconnect. Используется для разбиения кадров. */
static volatile uint16_t g_peer_mtu = 23;

/* Событие «шлюз готов»: mtu ставят onMTUChange/onSubscribe, отправляет
 * poll (0 = нет события). */
static volatile uint16_t g_ready_mtu = 0;

/* Кольцо входящих байтов: onWrite (nimble_host) пишет, poll (loopTask)
 * читает. Один производитель + один потребитель на одном ядре — без
 * блокировок. Ёмкость с запасом на несколько write за интервал связи. */
static uint8_t         g_rx_ring[2048];
static volatile size_t g_rx_w = 0;
static volatile size_t g_rx_r = 0;
static volatile uint32_t g_rx_dropped = 0;

static void rxRingWrite(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        size_t w = g_rx_w;
        size_t next = w + 1;
        if (next >= sizeof(g_rx_ring)) next = 0;
        if (next == g_rx_r) { g_rx_dropped = g_rx_dropped + 1; return; }
        g_rx_ring[w] = data[i];
        g_rx_w = next;
    }
}

static bool rxRingRead(uint8_t* out) {
    size_t r = g_rx_r;
    if (r == g_rx_w) return false;
    *out = g_rx_ring[r];
    r++;
    if (r >= sizeof(g_rx_ring)) r = 0;
    g_rx_r = r;
    return true;
}

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

/* Буфер разбора — статический: локальный char json[4097] на стеке задачи
 * вызывающего (в RX-цепочке — nimble_host) переполнял бы стек.
 * processCompleteFrame вызывается только из ble_gateway_poll() (loopTask),
 * поэтому один общий буфер безопасен. */
static char g_parse_json[MESH_FRAME_MAX + 1];

static void processCompleteFrame(const uint8_t* payload, size_t len) {
    /* payload — JSON */
    if (len > MESH_FRAME_MAX) return;
    memcpy(g_parse_json, payload, len);
    g_parse_json[len] = 0;
    const char* json = g_parse_json;

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

/* Разбор одного входного байта: ищем SOF, собираем по len.
 * Вызывается ТОЛЬКО из ble_gateway_poll() (loopTask): байты сначала
 * попадают в кольцо из коллбэка onWrite, здесь — потоковый парсер. */
static void parseByte(uint8_t b) {
    if (g_frame_len == 0) {
        if (b == MESH_FRAME_SOF) {
            g_frame_buf[g_frame_len++] = b;
        }
        return;
    }

    g_frame_buf[g_frame_len++] = b;

    if (g_frame_len == 3) {
        /* Заголовок получен: проверяем длину */
        size_t plen = (size_t)g_frame_buf[1] | ((size_t)g_frame_buf[2] << 8);
        if (plen == 0 || plen > MESH_FRAME_MAX - 5) {
            g_frame_len = 0;   /* отбрасываем мусор */
            return;
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

/* ---- Готовность шлюза ----
 * Клиент (ble.js) ждёт status {kind:"ready", mtu}, чтобы показать MTU
 * в панели состояния. Вызывается ТОЛЬКО из ble_gateway_poll() (loopTask):
 * коллбэки NimBLE лишь ставят флаг g_ready_mtu. */
static void sendReadyStatus(uint16_t mtu) {
    char json[64];
    snprintf(json, sizeof(json),
             "{\"t\":\"status\",\"kind\":\"ready\",\"mtu\":%u}", (unsigned)mtu);
    if (g_on_event) {
        g_on_event(json);
    } else {
        ble_gateway_send_json(json);
    }
}

/* ---- Колбэки NimBLE (API 2.x: NimBLEConnInfo&) ---- */
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        (void)pServer;
        g_client_connected = true;
        Serial.printf("[BLE] client connected (mtu=%u)\n",
                      (unsigned)connInfo.getMTU());
    }
    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        Serial.printf("[BLE] MTU exchanged: %u\n", (unsigned)MTU);
        /* Только флаг: реальную отправку ready делает poll (loopTask).
         * onMTUChange и onSubscribe могут прийти оба — отправка
         * идемпотентна, poll разгребает последний MTU. */
        if (g_client_connected) g_ready_mtu = MTU;
        g_peer_mtu = MTU;
    }
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        (void)pServer; (void)connInfo;
        g_client_connected = false;
        g_peer_mtu = 23;   /* на новом соединении MTU снова 23 до обмена */
        Serial.printf("[BLE] client disconnected (reason=%d)\n", reason);
        if (g_adv) {
            bool adv_ok = g_adv->start();
            Serial.printf("[BLE] advertising restart: %s\n", adv_ok ? "OK" : "FAILED");
        }
    }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        (void)connInfo;
        NimBLEAttValue v = chr->getValue();
        if (v.size() > 0) rxRingWrite(v.data(), v.size());
    }
};

/* Подписка клиента на TX-notify: момент, когда кадры до клиента дойдут */
class TxCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo,
                     uint16_t subValue) override {
        (void)chr;
        Serial.printf("[BLE] tx subscribed (sub=%u, mtu=%u)\n",
                      (unsigned)subValue, (unsigned)connInfo.getMTU());
        if (subValue & 0x0001) {   /* notifications enabled */
            /* Только флаг — саму отправку делает poll (loopTask). */
            uint16_t mtu = connInfo.getMTU();
            if (!mtu) mtu = g_peer_mtu;
            if (g_client_connected) g_ready_mtu = mtu;
        }
    }
};

static ServerCallbacks g_server_cb;
static RxCallbacks     g_rx_cb;
static TxCallbacks     g_tx_cb;

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
    g_tx->setCallbacks(&g_tx_cb);

    /* service->start() в NimBLE 2.x — no-op (deprecated): сервисы стартуют
     * вместе с NimBLEDevice::startServices() ниже. */

    g_adv = NimBLEDevice::getAdvertising();
    /* Имя ОБЯЗАТЕЛЬНО в advertising data — NimBLEDevice::init() кладёт его
     * только в GAP, но фильтр клиента { namePrefix: "MeshGate-" } ищет имя
     * в рекламе. Без setName() Chrome пишет «Нет устройств». */
    g_adv->setName(device_name);
    g_adv->addServiceUUID(MESH_SERVICE_UUID);
    /* Scan response включаем: туда уходит полное имя, если adv-packet
     * переполнен (31 байт) — иначе имя обрежется и фильтр не совпадёт. */
    g_adv->enableScanResponse(true);

    bool adv_ok = g_adv->start();
    Serial.println("[BLE] gateway started as " + String(device_name));
    Serial.printf("[BLE] advertising: %s (name=%s)\n",
                  adv_ok ? "OK" : "FAILED", device_name);
    if (!adv_ok) {
        Serial.println("[BLE] WARNING: advertising failed to start");
    }
    return adv_ok;
}

bool ble_gateway_connected() {
    return g_client_connected;
}

/* Буфер TX — статический: локальный uint8_t frame[4096] на стеке
 * вызывающего переполнял бы стек задачи nimble_host (sendReadyStatus
 * вызывался из onSubscribe/onMTUChange). send_frame теперь выполняется
 * только на loopTask (через poll → on_event/send_json), один общий
 * буфер безопасен. */
static uint8_t g_tx_frame[MESH_FRAME_MAX];

void ble_gateway_send_frame(const uint8_t* payload, size_t len) {
    if (!g_tx || !g_client_connected) return;
    if (len + 5 > MESH_FRAME_MAX) return;

    uint8_t* frame = g_tx_frame;
    size_t off = 0;
    frame[off++] = MESH_FRAME_SOF;
    frame[off++] = (uint8_t)(len & 0xff);
    frame[off++] = (uint8_t)((len >> 8) & 0xff);
    memcpy(&frame[off], payload, len);
    off += len;
    uint16_t crc = crc16(payload, len);
    frame[off++] = (uint8_t)(crc & 0xff);
    frame[off++] = (uint8_t)((crc >> 8) & 0xff);

    /* Кадр может быть длиннее MTU-3: клиентский парсер (web/js/protocol.js
     * createFeeder) потоковый и собирает кадр из нескольких notify.
     * Разбиваем на куски по MTU-3 (клампим до 247; при MTU<23 — 20).
     * Клиент шлёт свои кадры целиком (≤192 байта при MTU 23), это ок. */
    uint16_t mtu = g_peer_mtu;
    size_t chunk = (mtu >= 23) ? (size_t)(mtu - 3) : 20;
    if (chunk > 247) chunk = 247;
    if (chunk < 1) chunk = 1;

    g_tx->setValue(frame, off);
    g_tx->notify();
    for (size_t pos = chunk; pos < off; pos += chunk) {
        size_t n = (off - pos < chunk) ? (off - pos) : chunk;
        g_tx->setValue(&frame[pos], n);
        g_tx->notify();
    }
}

/* Экранирование строки для JSON: кавычки, обратные слэши, управляющие
 * символы. Без этого текст сообщения со `"` или `\` ломает JSON всего
 * кадра. Экспортируется: main.cpp использует при сборке msg/name/nodes. */
void ble_gateway_json_escape(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < cap; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if ((unsigned char)c < 0x20) {
            out[o++] = ' ';
        } else {
            out[o++] = c;
        }
    }
    out[o] = 0;
}

void ble_gateway_send_json(const char* json) {
    if (!json) return;
    ble_gateway_send_frame((const uint8_t*)json, strlen(json));
}

/* Периодическое обслуживание. Вызывается из loop() на loopTask — здесь
 * допустима «тяжёлая» работа (буферы, send_json), которую убрали из
 * коллбэков nimble_host. */
void ble_gateway_poll() {
    /* 1) Готовность: коллбэки NimBLE (nimble_host) поставили MTU —
     *    шлём ready отсюда, с безопасного стека. */
    uint16_t ready = g_ready_mtu;
    if (ready != 0) {
        g_ready_mtu = 0;
        if (g_client_connected) sendReadyStatus(ready);
    }

    /* 2) Разбор входящих байтов из кольца (производитель — onWrite). */
    uint8_t b;
    while (rxRingRead(&b)) {
        parseByte(b);
    }

    /* 3) Переполнение кольца диагностируем (данные при этом потеряны). */
    uint32_t dropped = g_rx_dropped;
    if (dropped != 0) {
        g_rx_dropped = 0;
        Serial.printf("[BLE] WARNING: rx ring overflow, dropped %u bytes\n",
                      (unsigned)dropped);
    }
}

void ble_gateway_disconnect_client() {
    if (g_server && g_client_connected) {
        g_server->disconnect(0);
    }
}
