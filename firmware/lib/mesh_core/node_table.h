/* node_table.h — таблица узлов: online/offline, имена, соседи.
 * MESH_PROTOCOL §5. Различаем прямых соседей (HELLO с TTL=1) и mesh-узлы.
 */
#ifndef MESH_CORE_NODE_TABLE_H
#define MESH_CORE_NODE_TABLE_H

#include <stdint.h>
#include <stddef.h>
#include "packet.h"

namespace mesh {

struct NodeEntry {
    uint16_t id;
    char     name[kMaxName + 1];
    bool     neighbor;      /* прямой радиососед */
    bool     online;
    int8_t   rssi;
    uint32_t last_seen_ms;
    bool     used;
};

class NodeTable {
public:
    static const int kCapacity = 16;
    static const uint32_t kOnlineTimeoutMs = 30000;   /* §5: 3 пропущенных HELLO */

    NodeTable() { clear(); }

    void clear() {
        for (int i = 0; i < kCapacity; i++) m_entries[i].used = false;
    }

    /* Обработать входящий HELLO: обновить запись. */
    void onHello(uint16_t id, const char* name, bool neighbor,
                 int8_t rssi, uint32_t now_ms);

    /* Пометить offline узлы, не виденные дольше kOnlineTimeoutMs. */
    void sweep(uint32_t now_ms);

    /* Запись по id; nullptr, если неизвестен. */
    const NodeEntry* find(uint16_t id) const;

    /* Количество записей */
    int count() const;

    /* i-я использованная запись (для итерации); nullptr при выходе за границы */
    const NodeEntry* at(int index) const;

    /* Число прямых соседей / online */
    int neighborCount() const;
    int onlineCount() const;

private:
    int findIndex(uint16_t id) const;
    int allocSlot();

    NodeEntry m_entries[kCapacity];
};

} // namespace mesh

#endif /* MESH_CORE_NODE_TABLE_H */
