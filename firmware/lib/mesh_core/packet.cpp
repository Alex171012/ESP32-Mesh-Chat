/* packet.cpp — сериализация/десериализация пакета. MESH_PROTOCOL §2.
 * Без Arduino/ESP-IDF: только <stdint.h>, <string.h>.
 */
#include "packet.h"
#include <string.h>

namespace mesh {

/* ---- little-endian helpers ---- */
static inline void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}
static inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}
static inline uint16_t get16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void putHeader(uint8_t* out, const Header& h) {
    out[0]  = h.version;
    out[1]  = h.type;
    out[2]  = h.ttl;
    out[3]  = h.flags;
    put16(out + 4, h.dst);
    put16(out + 6, h.src);
    put16(out + 8, h.session);
    put32(out + 10, h.seq);
    out[14] = h.hop_count;
    out[15] = h.path_len;
}

static void getHeader(const uint8_t* in, Header& h) {
    h.version   = in[0];
    h.type      = in[1];
    h.ttl       = in[2];
    h.flags     = in[3];
    h.dst       = get16(in + 4);
    h.src       = get16(in + 6);
    h.session   = get16(in + 8);
    h.seq       = get32(in + 10);
    h.hop_count = in[14];
    h.path_len  = in[15];
}

size_t serialize(const Packet& p, uint8_t* out, size_t out_cap) {
    if (!out || out_cap < kHeaderSize) return 0;

    Header h = p.hdr;
    h.version = kVersion;

    size_t body_len = 0;
    switch (h.type) {
    case TYPE_DATA: {
        const DataBody& d = p.body.data;
        /* path_len берём из заголовка — он источник истины (MESH_PROTOCOL §2.1) */
        if (h.path_len > kMaxPath || d.text_len > kMaxText) return 0;
        body_len = (size_t)h.path_len * 2 + 1 + d.text_len;
        break;
    }
    case TYPE_ACK: {
        if (h.path_len > kMaxPath) return 0;
        body_len = (size_t)h.path_len * 2 + 8 + 1;
        break;
    }
    case TYPE_HELLO: {
        const HelloBody& hl = p.body.hello;
        if (hl.name_len > kMaxName || hl.neighbor_count > kMaxPath) return 0;
        body_len = 1 + hl.name_len + 1 + (size_t)hl.neighbor_count * 2;
        break;
    }
    default:
        return 0;
    }

    size_t total = kHeaderSize + body_len;
    if (total > kMaxPacket || total > out_cap) return 0;

    putHeader(out, h);
    size_t off = kHeaderSize;

    switch (h.type) {
    case TYPE_DATA: {
        const DataBody& d = p.body.data;
        memcpy(out + off, d.path, (size_t)h.path_len * 2);
        off += (size_t)h.path_len * 2;
        out[off++] = d.text_len;
        memcpy(out + off, d.text, d.text_len);
        off += d.text_len;
        break;
    }
    case TYPE_ACK: {
        const AckBody& a = p.body.ack;
        memcpy(out + off, a.path, (size_t)h.path_len * 2);
        off += (size_t)h.path_len * 2;
        memcpy(out + off, a.msg_id, 8);
        off += 8;
        out[off++] = a.status;
        break;
    }
    case TYPE_HELLO: {
        const HelloBody& hl = p.body.hello;
        out[off++] = hl.name_len;
        memcpy(out + off, hl.name, hl.name_len);
        off += hl.name_len;
        out[off++] = hl.neighbor_count;
        memcpy(out + off, hl.neighbors, (size_t)hl.neighbor_count * 2);
        off += (size_t)hl.neighbor_count * 2;
        break;
    }
    default:
        return 0;
    }

    return off;
}

bool deserialize(const uint8_t* in, size_t in_len, Packet& out) {
    if (!in || in_len < kHeaderSize || in_len > kMaxPacket) return false;

    Header h;
    getHeader(in, h);
    if (h.version != kVersion) return false;
    if (h.path_len > kMaxPath) return false;

    memset(&out, 0, sizeof(out));
    out.hdr = h;

    const uint8_t* body = in + kHeaderSize;
    size_t body_len = in_len - kHeaderSize;

    switch (h.type) {
    case TYPE_DATA: {
        /* Тело: [path(path_len*2)][text_len(1)][text(text_len)] — §2.4 */
        size_t path_bytes = (size_t)h.path_len * 2;
        if (body_len < path_bytes + 1) return false;
        uint8_t text_len = body[path_bytes];
        size_t expect = path_bytes + 1 + text_len;
        if (expect != body_len) return false;
        if (text_len > kMaxText) return false;

        DataBody& d = out.body.data;
        d.path_len = h.path_len;
        memcpy(d.path, body, path_bytes);
        d.text_len = text_len;
        memcpy(d.text, body + path_bytes + 1, text_len);
        d.text[text_len] = 0;
        break;
    }
    case TYPE_ACK: {
        size_t expect = (size_t)h.path_len * 2 + 8 + 1;
        if (expect != body_len) return false;
        AckBody& a = out.body.ack;
        a.path_len = h.path_len;
        memcpy(a.path, body, (size_t)h.path_len * 2);
        memcpy(a.msg_id, body + (size_t)h.path_len * 2, 8);
        a.status = body[body_len - 1];
        break;
    }
    case TYPE_HELLO: {
        if (body_len < 2) return false;
        uint8_t name_len = body[0];
        if ((size_t)1 + name_len + 1 > body_len) return false;
        uint8_t nc = body[1 + name_len];
        size_t expect = 1 + (size_t)name_len + 1 + (size_t)nc * 2;
        if (expect != body_len) return false;
        if (name_len > kMaxName || nc > kMaxPath) return false;

        HelloBody& hl = out.body.hello;
        hl.name_len = name_len;
        memcpy(hl.name, body + 1, name_len);
        hl.name[name_len] = 0;
        hl.neighbor_count = nc;
        memcpy(hl.neighbors, body + 1 + name_len + 1, (size_t)nc * 2);
        break;
    }
    default:
        return false;
    }

    return true;
}

} // namespace mesh
