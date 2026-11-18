/* msg_id.h — формирование и сравнение msg_id. MESH_PROTOCOL §1.4.
 * msg_id = node_id(16) | session_id(16) | seq(32)  →  8 байт.
 */
#ifndef MESH_CORE_MSG_ID_H
#define MESH_CORE_MSG_ID_H

#include <stdint.h>
#include <string.h>

namespace mesh {

struct MsgId {
    uint16_t node;
    uint16_t session;
    uint32_t seq;

    bool operator==(const MsgId& o) const {
        return node == o.node && session == o.session && seq == o.seq;
    }
    bool operator!=(const MsgId& o) const { return !(*this == o); }
};

inline MsgId makeMsgId(uint16_t node, uint16_t session, uint32_t seq) {
    MsgId m; m.node = node; m.session = session; m.seq = seq; return m;
}

/* Ключ кэша дубликатов: 8 байт в порядке node|session|seq (little-endian).
 * Занимает ровно 8 байт, кладётся в кольцевой буфер как есть.
 */
inline void packMsgId(const MsgId& m, uint8_t out[8]) {
    out[0] = (uint8_t)(m.node & 0xff);
    out[1] = (uint8_t)((m.node >> 8) & 0xff);
    out[2] = (uint8_t)(m.session & 0xff);
    out[3] = (uint8_t)((m.session >> 8) & 0xff);
    out[4] = (uint8_t)(m.seq & 0xff);
    out[5] = (uint8_t)((m.seq >> 8) & 0xff);
    out[6] = (uint8_t)((m.seq >> 16) & 0xff);
    out[7] = (uint8_t)((m.seq >> 24) & 0xff);
}

inline MsgId unpackMsgId(const uint8_t in[8]) {
    MsgId m;
    m.node    = (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
    m.session = (uint16_t)(in[2] | ((uint16_t)in[3] << 8));
    m.seq     = (uint32_t)in[4] | ((uint32_t)in[5] << 8) |
                ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    return m;
}

inline MsgId msgIdFromHeader(uint16_t node, uint16_t session, uint32_t seq) {
    return makeMsgId(node, session, seq);
}

} // namespace mesh

#endif /* MESH_CORE_MSG_ID_H */
