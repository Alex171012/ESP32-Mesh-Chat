/* node_table.cpp — реализация таблицы узлов. MESH_PROTOCOL §5. */
#include "node_table.h"
#include <string.h>

namespace mesh {

int NodeTable::findIndex(uint16_t id) const {
    for (int i = 0; i < kCapacity; i++) {
        if (m_entries[i].used && m_entries[i].id == id) return i;
    }
    return -1;
}

int NodeTable::allocSlot() {
    for (int i = 0; i < kCapacity; i++) {
        if (!m_entries[i].used) return i;
    }
    /* Вытесняем самый старый (наименьший last_seen) */
    int oldest = 0;
    for (int i = 1; i < kCapacity; i++) {
        if (m_entries[i].last_seen_ms < m_entries[oldest].last_seen_ms) oldest = i;
    }
    return oldest;
}

void NodeTable::onHello(uint16_t id, const char* name, bool neighbor,
                        int8_t rssi, uint32_t now_ms) {
    if (id == 0x0000 || id == 0xFFFF) return;   /* зарезервированные */

    int idx = findIndex(id);
    if (idx < 0) idx = allocSlot();

    NodeEntry& e = m_entries[idx];
    e.id = id;
    e.neighbor = neighbor;
    e.online = true;
    e.rssi = rssi;
    e.last_seen_ms = now_ms;
    e.used = true;

    if (name && name[0]) {
        strncpy(e.name, name, kMaxName);
        e.name[kMaxName] = 0;
    }
}

void NodeTable::sweep(uint32_t now_ms) {
    for (int i = 0; i < kCapacity; i++) {
        if (!m_entries[i].used) continue;
        if ((uint32_t)(now_ms - m_entries[i].last_seen_ms) > kOnlineTimeoutMs) {
            m_entries[i].online = false;
        }
    }
}

const NodeEntry* NodeTable::find(uint16_t id) const {
    int idx = findIndex(id);
    return idx < 0 ? nullptr : &m_entries[idx];
}

int NodeTable::count() const {
    int n = 0;
    for (int i = 0; i < kCapacity; i++) if (m_entries[i].used) n++;
    return n;
}

const NodeEntry* NodeTable::at(int index) const {
    if (index < 0 || index >= kCapacity) return nullptr;
    return m_entries[index].used ? &m_entries[index] : nullptr;
}

int NodeTable::neighborCount() const {
    int n = 0;
    for (int i = 0; i < kCapacity; i++) if (m_entries[i].used && m_entries[i].neighbor) n++;
    return n;
}

int NodeTable::onlineCount() const {
    int n = 0;
    for (int i = 0; i < kCapacity; i++) if (m_entries[i].used && m_entries[i].online) n++;
    return n;
}

} // namespace mesh
