/* relay.cpp — реализация правил пересылки. MESH_PROTOCOL §3, §4, §6. */
#include "relay.h"
#include <string.h>

namespace mesh {

Decision evaluate(const Packet& in, const RelayContext& ctx, Packet& out) {
    out = in;

    /* 1. версия */
    if (in.hdr.version != kVersion) return DEC_DROP;

    /* 2. пакет от меня самого — защита от петли */
    if (in.hdr.src == ctx.my_node) return DEC_DROP;

    /* 3. msg_id уже в кэше дубликатов — HELLO не кэшируется (§5) */
    if (in.hdr.type != TYPE_HELLO && ctx.dedup) {
        if (ctx.dedup->seen(msgIdOf(in), ctx.now_ms)) return DEC_DROP;
    }

    bool recipient = isRecipient(in, ctx.my_node);

    /* 4. ttl == 0: обработать локально, но не пересылать */
    if (in.hdr.ttl == 0) {
        return recipient ? DEC_LOCAL : DEC_DROP;
    }

    /* 5. мой node_id уже в path — защита от петли по маршруту.
     *    Исключение: ACK возвращается по перевёрнутому path, и я там буду.
     *    Поэтому для ACK этот шаг пропускаем. */
    if (in.hdr.type != TYPE_ACK && in.hdr.path_len > 0) {
        const uint8_t* path = in.body.data.path;
        if (in.hdr.type == TYPE_DATA) {
            for (uint8_t i = 0; i < in.hdr.path_len; i++) {
                uint16_t hop = (uint16_t)(path[i*2] | ((uint16_t)path[i*2+1] << 8));
                if (hop == ctx.my_node) return DEC_DROP;
            }
        }
    }

    /* Кэшируем до пересылки — §3.1, «иначе гонка при быстрых повторных приёмах» */
    if (in.hdr.type != TYPE_HELLO && ctx.dedup) {
        ctx.dedup->remember(msgIdOf(in), ctx.now_ms);
    }

    if (!recipient) return DEC_RELAY;

    /* Я получатель. Broadcast: обработать и переслить дальше. */
    if (in.hdr.dst == 0x0000 && in.hdr.type == TYPE_DATA) {
        return DEC_LOCAL_RELAY;
    }
    return DEC_LOCAL;
}

bool prepareRelay(Packet& p, uint16_t my_node) {
    if (p.hdr.ttl == 0) return false;

    /* Добавляем себя в path (только для DATA — §6.2 для ACK не нужен) */
    if (p.hdr.type == TYPE_DATA) {
        if (p.hdr.path_len >= kMaxPath) return false;
        uint8_t* path = p.body.data.path;
        path[p.hdr.path_len * 2]     = (uint8_t)(my_node & 0xff);
        path[p.hdr.path_len * 2 + 1] = (uint8_t)((my_node >> 8) & 0xff);
        p.hdr.path_len++;
    }

    p.hdr.ttl--;
    p.hdr.hop_count++;
    return true;
}

void buildAck(const Packet& data, uint16_t my_node, uint8_t status, Packet& ack) {
    memset(&ack, 0, sizeof(ack));
    ack.hdr.version = kVersion;
    ack.hdr.type    = TYPE_ACK;
    ack.hdr.ttl     = data.hdr.ttl + 1;   /* восстанавливаем запас на обратный путь */
    ack.hdr.flags   = FLAG_IS_ACK;
    ack.hdr.dst     = data.hdr.src;       /* конечный получатель — исходный отправитель */
    ack.hdr.src     = my_node;
    ack.hdr.session = data.hdr.session;
    ack.hdr.seq     = data.hdr.seq;
    ack.hdr.hop_count = data.hdr.hop_count;
    ack.hdr.path_len  = 0;

    /* Разворачиваем path исходного DATA: узел-получатель последним в path,
     * значит первый элемент обратного path — узел, от которого пришёл DATA. */
    for (int i = data.hdr.path_len - 1; i >= 0; i--) {
        ack.body.ack.path[ack.hdr.path_len * 2]     = data.body.data.path[i*2];
        ack.body.ack.path[ack.hdr.path_len * 2 + 1] = data.body.data.path[i*2 + 1];
        ack.hdr.path_len++;
    }

    MsgId mid = msgIdOf(data);
    packMsgId(mid, ack.body.ack.msg_id);
    ack.body.ack.status = status;
}

bool nextHopForAck(const Packet& ack, uint16_t& next_hop) {
    /* ACK идёт по перевёрнутому path. Первый элемент — адресат следующего шага. */
    if (ack.hdr.path_len == 0) return false;
    next_hop = (uint16_t)(ack.body.ack.path[0] |
                          ((uint16_t)ack.body.ack.path[1] << 8));
    return true;
}

} // namespace mesh
