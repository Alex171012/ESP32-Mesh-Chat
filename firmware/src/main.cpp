/* main.cpp — setup/loop, склейка модулей. ARCHITECTURE §2–4.
 *
 * Одна прошивка на все три платы. Роль шлюза включается, когда к узлу
 * подключается BLE-клиент (ARCHITECTURE §5).
 *
 * Правило: никаких delay() в цикле — всё на millis() (ARCHITECTURE §4).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>

#include "config.h"
#include "espnow_link.h"
#include "ble_gateway.h"

#include "../lib/mesh_core/packet.h"
#include "../lib/mesh_core/msg_id.h"
#include "../lib/mesh_core/relay.h"
#include "../lib/mesh_core/dedup_cache.h"
#include "../lib/mesh_core/node_table.h"

using namespace mesh;

/* ---- Состояние узла ---- */
static uint16_t  g_node_id = 0;
static uint16_t  g_session = 0;
static uint32_t  g_seq = 0;
static char      g_name[MESH_NAME_LIMIT + 1] = {0};
static bool      g_name_assigned = false;

static DedupCache g_dedup;
static NodeTable  g_nodes;

/* Таймеры (ARCHITECTURE §4) */
static uint32_t g_last_hello = 0;
static uint32_t g_last_dedup_sweep = 0;
static uint32_t g_last_node_sweep = 0;

/* Ожидание ACK: msg_id → время отправки */
struct PendingAck {
    bool     used;
    MsgId    id;
    uint32_t sent_ms;
    uint16_t dst;
    char     text[MESH_TEXT_LIMIT + 1];
};
static const int kMaxPending = 8;
static PendingAck g_pending[kMaxPending];

/* ---- Вспомогательные функции ---- */

static void loadNameFromNvs() {
    /* [ПРОВЕРИТЬ] Preferences на железе — здесь заглушка:
     * имя не задано, пока клиент не вызовет set_name. */
    g_name[0] = 0;
    g_name_assigned = false;
}

static void saveNameToNvs(const char* n) {
    strncpy(g_name, n, MESH_NAME_LIMIT);
    g_name[MESH_NAME_LIMIT] = 0;
    g_name_assigned = true;
    /* [ПРОВЕРИТЬ] Preferences::putString на реальной плате */
}

static uint16_t nodeIdFromMac() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    /* Младшие 2 байта — node_id (MESH_PROTOCOL §1.1) */
    uint16_t id = (uint16_t)(mac[4] | ((uint16_t)mac[5] << 8));
    if (id == 0x0000) id = 0x0001;
    if (id == 0xFFFF) id = 0xFFFE;
    return id;
}

static uint16_t makeSession() {
    return (uint16_t)(esp_random() & 0xFFFF);
}

static uint32_t nextSeq() {
    return ++g_seq;
}

/* node_id → MAC для unicast: ESP-NOW peer MAC = station MAC получателя.
 * В v1 используется broadcast для прохождения через посредника.
 * [ПРОВЕРИТЬ] на железе: как получить MAC соседа из node_id. */
static void macForNode(uint16_t node, uint8_t mac[6]) {
    (void)node;
    memset(mac, 0xFF, 6);   /* placeholder: реальный MAC узнаём из HELLO */
}

static void sendJson(const char* json) {
    ble_gateway_send_json(json);
}

static void sendStatus(const MsgId& id, const char* state,
                       const uint8_t* path, uint8_t path_len,
                       uint32_t rtt_ms) {
    if (!ble_gateway_connected()) return;
    char json[512];
    char path_str[256] = {0};
    size_t po = 0;
    path_str[po++] = '[';
    for (uint8_t i = 0; i < path_len && po < sizeof(path_str) - 8; i++) {
        if (i) path_str[po++] = ',';
        uint16_t hop = (uint16_t)(path[i*2] | ((uint16_t)path[i*2+1] << 8));
        po += snprintf(&path_str[po], sizeof(path_str) - po, "%u", hop);
    }
    path_str[po++] = ']';
    path_str[po] = 0;

    if (rtt_ms > 0) {
        snprintf(json, sizeof(json),
                 "{\"t\":\"status\",\"id\":\"%08x%08x\",\"state\":\"%s\","
                 "\"path\":%s,\"rtt_ms\":%u}",
                 (unsigned)id.node, (unsigned)id.seq, state, path_str,
                 (unsigned)rtt_ms);
    } else {
        snprintf(json, sizeof(json),
                 "{\"t\":\"status\",\"id\":\"%08x%08x\",\"state\":\"%s\","
                 "\"path\":%s}",
                 (unsigned)id.node, (unsigned)id.seq, state, path_str);
    }
    sendJson(json);
}

/* ---- Отправка DATA ---- */

static bool sendData(uint16_t dst, const char* text, bool ack_req) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = MESH_DEFAULT_TTL;
    p.hdr.flags = FLAG_BROADCAST | (ack_req ? FLAG_ACK_REQ : 0);
    p.hdr.dst = dst;
    p.hdr.src = g_node_id;
    p.hdr.session = g_session;
    p.hdr.seq = nextSeq();
    p.hdr.hop_count = 0;
    p.hdr.path_len = 0;

    size_t tlen = strlen(text);
    if (tlen > MESH_TEXT_LIMIT) tlen = MESH_TEXT_LIMIT;
    p.body.data.text_len = (uint8_t)tlen;
    memcpy(p.body.data.text, text, tlen);

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    if (n == 0) return false;

    uint8_t mac[6];
    macForNode(dst, mac);
    bool ok = (dst == 0x0000)
        ? espnow_link_send_broadcast(buf, n)
        : espnow_link_send(mac, buf, n);

    if (!ok) return false;

    MsgId id = msgIdOf(p);
    if (ack_req) {
        /* Запоминаем для ожидания ACK */
        for (int i = 0; i < kMaxPending; i++) {
            if (!g_pending[i].used) {
                g_pending[i].used = true;
                g_pending[i].id = id;
                g_pending[i].sent_ms = millis();
                g_pending[i].dst = dst;
                strncpy(g_pending[i].text, text, MESH_TEXT_LIMIT);
                break;
            }
        }
    }
    sendStatus(id, "sent", nullptr, 0, 0);
    return true;
}

/* ---- Обработка входящих ESP-NOW ---- */

/* forward declaration — onAck определена ниже onEspNowRecv */
static void onAck(const Packet& ack);

static void onEspNowRecv(const uint8_t* data, size_t len,
                         const uint8_t* src_mac, int8_t rssi) {
    Packet in;
    if (!deserialize(data, len, in)) return;

    RelayContext ctx = { g_node_id, &g_dedup, millis() };
    Packet out;
    Decision d = evaluate(in, ctx, out);

    if (d == DEC_DROP) return;

    /* Я получатель: обработать локально */
    if (d == DEC_LOCAL || d == DEC_LOCAL_RELAY) {
        if (in.hdr.type == TYPE_ACK) {
            /* Подтверждение доставки → клиенту (MESH_PROTOCOL §6.3) */
            onAck(in);
        } else if (in.hdr.type == TYPE_DATA) {
            /* Сообщение от другого узла → клиенту по BLE */
            if (ble_gateway_connected() && in.hdr.src != g_node_id) {
                char json[1024];
                const NodeEntry* src = g_nodes.find(in.hdr.src);
                /* Экранируем имя и текст: `"` в них ломает JSON всего кадра */
                char src_name_esc[64];
                ble_gateway_json_escape(
                    (src && src->name[0]) ? src->name : "",
                    src_name_esc, sizeof(src_name_esc));
                char text_esc[512];
                ble_gateway_json_escape((const char*)in.body.data.text,
                                        text_esc, sizeof(text_esc));
                char path_str[256] = {0};
                size_t po = 0;
                path_str[po++] = '[';
                for (uint8_t i = 0; i < in.hdr.path_len; i++) {
                    if (i) path_str[po++] = ',';
                    uint16_t hop = (uint16_t)(in.body.data.path[i*2] |
                                               ((uint16_t)in.body.data.path[i*2+1] << 8));
                    po += snprintf(&path_str[po], sizeof(path_str) - po, "%u", hop);
                }
                path_str[po++] = ']';
                path_str[po] = 0;

                snprintf(json, sizeof(json),
                         "{\"t\":\"msg\",\"id\":\"%08x%08x\",\"src\":%u,"
                         "\"src_name\":\"%s\",\"dst\":%u,\"text\":\"%s\","
                         "\"path\":%s,\"hops\":%u,\"rssi\":%d,\"ts\":%u}",
                         (unsigned)in.hdr.src, (unsigned)in.hdr.seq,
                         (unsigned)in.hdr.src,
                         src_name_esc,
                         (unsigned)in.hdr.dst,
                         text_esc,
                         path_str,
                         (unsigned)in.hdr.hop_count,
                         (int)rssi,
                         (unsigned)millis());
                sendJson(json);
            }

            /* Отправить ACK, если запрашивали (MESH_PROTOCOL §6.2) */
            if (in.hdr.flags & FLAG_ACK_REQ) {
                Packet ack;
                buildAck(in, g_node_id, ACK_OK, ack);
                uint8_t abuf[kMaxPacket];
                size_t an = serialize(ack, abuf, sizeof(abuf));
                if (an > 0) {
                    uint8_t mac[6];
                    macForNode(in.hdr.src, mac);
                    espnow_link_send_broadcast(abuf, an);
                }
            }
        }
    }

    /* Пересылка */
    if (d == DEC_RELAY || d == DEC_LOCAL_RELAY) {
        if (prepareRelay(out, g_node_id)) {
            uint8_t buf[kMaxPacket];
            size_t n = serialize(out, buf, sizeof(buf));
            if (n > 0) espnow_link_send_broadcast(buf, n);
        }
    }

    /* HELLO → обновить таблицу узлов */
    if (in.hdr.type == TYPE_HELLO) {
        char nm[MESH_NAME_LIMIT + 1] = {0};
        memcpy(nm, in.body.hello.name, in.body.hello.name_len);
        g_nodes.onHello(in.hdr.src, nm, true, rssi, millis());
    }
}

/* ---- Отправка HELLO ---- */

static void sendHello() {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_HELLO;
    p.hdr.ttl = 1;
    p.hdr.flags = 0;
    p.hdr.dst = 0xFFFF;
    p.hdr.src = g_node_id;
    p.hdr.session = g_session;
    p.hdr.seq = g_seq;
    p.hdr.hop_count = 0;
    p.hdr.path_len = 0;

    size_t nlen = strlen(g_name);
    if (nlen > MESH_NAME_LIMIT) nlen = MESH_NAME_LIMIT;
    p.body.hello.name_len = (uint8_t)nlen;
    memcpy(p.body.hello.name, g_name, nlen);

    /* Соседи — те, кого мы знаем как прямых */
    uint8_t nc = 0;
    for (int i = 0; i < NodeTable::kCapacity && nc < kMaxPath; i++) {
        const NodeEntry* e = g_nodes.at(i);
        if (e && e->neighbor) {
            p.body.hello.neighbors[nc*2]     = (uint8_t)(e->id & 0xff);
            p.body.hello.neighbors[nc*2 + 1] = (uint8_t)((e->id >> 8) & 0xff);
            nc++;
        }
    }
    p.body.hello.neighbor_count = nc;

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    if (n > 0) espnow_link_send_broadcast(buf, n);
}

/* ---- Обработка ACK ---- */

static void onAck(const Packet& ack) {
    MsgId id = unpackMsgId(ack.body.ack.msg_id);
    for (int i = 0; i < kMaxPending; i++) {
        if (!g_pending[i].used || g_pending[i].id != id) continue;
        uint32_t rtt = millis() - g_pending[i].sent_ms;
        const char* state = (ack.body.ack.status == ACK_OK) ? "delivered" : "failed";
        sendStatus(id, state, ack.body.ack.path, ack.body.ack.path_len, rtt);
        g_pending[i].used = false;
        return;
    }
}

/* ---- Колбэк BLE-команд ---- */

static void onBleCmd(const BleCommand& cmd) {
    switch (cmd.type) {
    case BLE_CMD_SEND: {
        bool ok = sendData(cmd.dst, cmd.text, cmd.ack);
        /* Принято шлюзом — сразу (BLE_API §4.2) */
        if (ble_gateway_connected()) {
            char json[128];
            snprintf(json, sizeof(json),
                     "{\"t\":\"status\",\"id\":\"local\",\"state\":\"%s\"}",
                     ok ? "accepted" : "failed");
            sendJson(json);
        }
        break;
    }
    case BLE_CMD_NODES: {
        /* Список узлов: соседи и mesh раздельно (BLE_API §4.2) */
        char json[1024];
        size_t off = snprintf(json, sizeof(json), "{\"t\":\"nodes\",\"list\":[");
        for (int i = 0; i < NodeTable::kCapacity; i++) {
            const NodeEntry* e = g_nodes.at(i);
            if (!e) continue;
            char name_esc[64];
            ble_gateway_json_escape(e->name, name_esc, sizeof(name_esc));
            off += snprintf(&json[off], sizeof(json) - off,
                "%s{\"id\":%u,\"name\":\"%s\",\"neighbor\":%s,"
                "\"online\":%s,\"rssi\":%d,\"last_seen_ms\":%u}",
                (i == 0) ? "" : ",",
                (unsigned)e->id, name_esc,
                e->neighbor ? "true" : "false",
                e->online ? "true" : "false",
                (int)e->rssi,
                (unsigned)(millis() - e->last_seen_ms));
            if (off >= sizeof(json) - 10) break;
        }
        snprintf(&json[off], sizeof(json) - off, "]}");
        sendJson(json);
        break;
    }
    case BLE_CMD_PING: {
        char json[64];
        snprintf(json, sizeof(json), "{\"t\":\"pong\",\"id\":%u}",
                 (unsigned)cmd.ping_id);
        sendJson(json);
        break;
    }
    case BLE_CMD_GET_NAME: {
        char json[128];
        if (g_name_assigned) {
            char name_esc[64];
            ble_gateway_json_escape(g_name, name_esc, sizeof(name_esc));
            snprintf(json, sizeof(json),
                     "{\"t\":\"name\",\"name\":\"%s\",\"assigned\":true}", name_esc);
        } else {
            snprintf(json, sizeof(json),
                     "{\"t\":\"name\",\"name\":null,\"assigned\":false}");
        }
        sendJson(json);
        break;
    }
    case BLE_CMD_SET_NAME: {
        if (strlen(cmd.name) > MESH_NAME_LIMIT) {
            sendJson("{\"t\":\"err\",\"code\":\"name_too_long\"}");
            break;
        }
        saveNameToNvs(cmd.name);
        char json[128];
        char name_esc[64];
        ble_gateway_json_escape(g_name, name_esc, sizeof(name_esc));
        snprintf(json, sizeof(json),
                 "{\"t\":\"name\",\"name\":\"%s\",\"assigned\":true}", name_esc);
        sendJson(json);
        break;
    }
    case BLE_CMD_CLEAR_NAME: {
        g_name[0] = 0;
        g_name_assigned = false;
        sendJson("{\"t\":\"name\",\"name\":null,\"assigned\":false}");
        break;
    }
    default:
        break;
    }
}

static void onBleEvent(const char* json) {
    /* Шлюз сообщает клиенту о готовности: {"t":"status","kind":"ready",
     * "mtu":N} — приходит после подписки клиента на notify и после
     * обмена MTU (BLE_API §4.1). Пересылаем как есть. */
    sendJson(json);
}

/* ---- Колбэк ESP-NOW отправки ---- */
static void onEspNowSend(const uint8_t* mac, int status) {
    (void)mac;
    (void)status;
}

/* ---- setup / loop ---- */

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[boot] ESP32-C3 Mesh Chat");

    pinMode(MESH_LED_PIN, OUTPUT);
    digitalWrite(MESH_LED_PIN, LOW);

    g_node_id = nodeIdFromMac();
    g_session = makeSession();
    loadNameFromNvs();

    Serial.printf("[node] id=0x%04X session=0x%04X\n", g_node_id, g_session);

    /* Радио */
    if (!espnow_link_init(onEspNowRecv, onEspNowSend)) {
        Serial.println("[espnow] init FAILED");
        while (true) { delay(1000); }
    }
    Serial.println("[espnow] init ok");

    /* BLE-шлюз */
    char dev_name[32];
    snprintf(dev_name, sizeof(dev_name), "%s%04X", MESH_BLE_NAME_PREFIX, g_node_id);
    if (!ble_gateway_init(dev_name, onBleCmd, onBleEvent)) {
        Serial.println("[ble] init FAILED ( continuing without gateway )");
    }

    /* Первый HELLO сразу */
    sendHello();
    g_last_hello = millis();
}

void loop() {
    uint32_t now = millis();

    /* HELLO каждые 10 с (MESH_PROTOCOL §5) */
    if (now - g_last_hello >= MESH_HELLO_PERIOD_MS) {
        g_last_hello = now;
        sendHello();
    }

    /* Чистка кэша дубликатов — раз в секунду */
    if (now - g_last_dedup_sweep >= MESH_DEDUP_SWEEP_MS) {
        g_last_dedup_sweep = now;
        g_dedup.sweep(now);
    }

    /* Проверка online/offline — каждые 5 с */
    if (now - g_last_node_sweep >= MESH_NODE_SWEEP_MS) {
        g_last_node_sweep = now;
        g_nodes.sweep(now);
    }

    /* Тайм-аут ожидания ACK — 5 с */
    for (int i = 0; i < kMaxPending; i++) {
        if (!g_pending[i].used) continue;
        if (now - g_pending[i].sent_ms > MESH_ACK_TIMEOUT_MS) {
            sendStatus(g_pending[i].id, "failed", nullptr, 0, 0);
            g_pending[i].used = false;
        }
    }

    ble_gateway_poll();

    /* Мигание LED при ID_CONFLICT — [ПРОВЕРИТЬ] на железе */
    delay(1);   /* маленькая пауза, чтобы не занимать CPU на 100% */
}
