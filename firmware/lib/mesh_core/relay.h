/* relay.h — правила пересылки: TTL, path, защита от петель.
 * MESH_PROTOCOL §3, §4, §6.
 * Проверки идут строго в порядке §3: первая сработавшая останавливает обработку.
 */
#ifndef MESH_CORE_RELAY_H
#define MESH_CORE_RELAY_H

#include <stdint.h>
#include <stddef.h>
#include "packet.h"
#include "msg_id.h"
#include "dedup_cache.h"

namespace mesh {

/* Решение, принятое при проверке пакета */
enum Decision : uint8_t {
    DEC_DROP,        /* отбросить, не обрабатывать */
    DEC_LOCAL,       /* я получатель, обработать локально, не пересылать */
    DEC_RELAY,       /* я посредник, переслать дальше */
    DEC_LOCAL_RELAY  /* я получатель И посредник (broadcast) */
};

struct RelayContext {
    uint16_t my_node;
    DedupCache* dedup;
    uint32_t now_ms;
};

/* Обработать входящий пакет: обновить кэш, решить судьбу.
 * Для DEC_RELAY / DEC_LOCAL_RELAY пакет уже модифицирован (ttl--, hop_count++,
 * свой node_id добавлен в path) — см. prepareRelay().
 */
Decision evaluate(const Packet& in, const RelayContext& ctx, Packet& out);

/* Подготовить пакет к пересылке: уменьшить ttl, увеличить hop_count,
 * добавить свой node_id в path. Возвращает false, если path переполнен
 * или ttl уже 0 (нельзя пересылать).
 */
bool prepareRelay(Packet& p, uint16_t my_node);

/* Являюсь ли я получателем: dst == 0 (broadcast) или dst == my_node */
inline bool isRecipient(const Packet& p, uint16_t my_node) {
    return p.hdr.dst == 0x0000 || p.hdr.dst == my_node;
}

/* Является ли пакет подтверждением */
inline bool isAck(const Packet& p) { return p.hdr.type == TYPE_ACK; }

/* Извлечь msg_id из заголовка */
inline MsgId msgIdOf(const Packet& p) {
    return makeMsgId(p.hdr.src, p.hdr.session, p.hdr.seq);
}

/* Собрать ACK: развернуть path исходного DATA, dst = исходный src.
 * MESH_PROTOCOL §6.2: при обратной пересылке узлы НЕ добавляют себя в path.
 */
void buildAck(const Packet& data, uint16_t my_node, uint8_t status, Packet& ack);

/* Обратный путь ACK: dst берётся из path[0] (развернутый), не из header.src.
 * Возвращает true, если путь ещё не пуст (есть куда идти).
 */
bool nextHopForAck(const Packet& ack, uint16_t& next_hop);

} // namespace mesh

#endif /* MESH_CORE_RELAY_H */
