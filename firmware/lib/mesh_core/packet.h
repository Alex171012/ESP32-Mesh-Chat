/* packet.h — формат пакета ESP-NOW: заголовок, типы, сериализация.
 * MESH_PROTOCOL.md §2. Правило: без Arduino.h / esp_now.h / WiFi.h.
 * Все многобайтовые поля little-endian (нативно для ESP32).
 */
#ifndef MESH_CORE_PACKET_H
#define MESH_CORE_PACKET_H

#include <stdint.h>
#include <stddef.h>

namespace mesh {

/* Жёсткий лимит пакета — MESH_PROTOCOL §2.5 */
static const uint16_t kMaxPacket   = 200;
static const uint8_t  kHeaderSize  = 16;
static const uint8_t  kVersion     = 1;
static const uint8_t  kMaxPath     = 32;      /* 2 байта на переход */
static const uint8_t  kMaxText     = 179;     /* лимит клиента, BLE_API §3.2 */
static const uint8_t  kMaxName     = 24;      /* имя узла, BLE_API §4.1 */

/* Типы пакетов — MESH_PROTOCOL §2.2 */
enum PacketType : uint8_t {
    TYPE_DATA     = 0x01,
    TYPE_ACK      = 0x02,
    TYPE_HELLO    = 0x03,
    TYPE_ROUTE_REQ = 0x04
};

/* Флаги — MESH_PROTOCOL §2.3 */
enum PacketFlags : uint8_t {
    FLAG_BROADCAST = 1 << 0,
    FLAG_ACK_REQ   = 1 << 1,
    FLAG_IS_ACK    = 1 << 2,
    FLAG_NO_RELAY  = 1 << 3
};

/* Статус ACK — MESH_PROTOCOL §2.4 */
enum AckStatus : uint8_t {
    ACK_OK        = 0,   /* доставлено */
    ACK_TTL_EXPIRE = 1,   /* TTL истёк */
    ACK_NO_ROUTE  = 2    /* нет такого узла */
};

/* Заголовок, 16 байт — MESH_PROTOCOL §2.1 */
struct Header {
    uint8_t  version;
    uint8_t  type;
    uint8_t  ttl;
    uint8_t  flags;
    uint16_t dst;
    uint16_t src;
    uint16_t session;
    uint32_t seq;
    uint8_t  hop_count;
    uint8_t  path_len;
};

/* Тело DATA: path[] + text — MESH_PROTOCOL §2.4 */
struct DataBody {
    uint8_t  path[kMaxPath];
    uint8_t  path_len;              /* число узлов (по 2 байта) */
    uint8_t  text[kMaxText + 1];
    uint8_t  text_len;
};

/* Тело ACK: path[] + acked_msg_id + status */
struct AckBody {
    uint8_t  path[kMaxPath];
    uint8_t  path_len;
    uint8_t  msg_id[8];             /* node_id + session + seq */
    uint8_t  status;
};

/* Тело HELLO: name + neighbors */
struct HelloBody {
    uint8_t  name[kMaxName];
    uint8_t  name_len;
    uint8_t  neighbors[kMaxPath];
    uint8_t  neighbor_count;
};

/* Распакованный пакет */
struct Packet {
    Header   hdr;
    union {
        DataBody  data;
        AckBody   ack;
        HelloBody hello;
    } body;
};

/* Сериализация: packet → байты. Возвращает длину или 0 при ошибке.
 * Формат: [16 байт заголовка][тело в зависимости от type]
 */
size_t serialize(const Packet& p, uint8_t* out, size_t out_cap);

/* Десериализация: байты → packet. false при любой ошибке (обрезка,
 * неверная длина, плохая версия, переполнение).
 */
bool deserialize(const uint8_t* in, size_t in_len, Packet& out);

} // namespace mesh

#endif /* MESH_CORE_PACKET_H */
