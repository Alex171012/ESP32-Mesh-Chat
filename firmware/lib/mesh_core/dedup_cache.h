/* dedup_cache.h — кольцевой кэш дубликатов. MESH_PROTOCOL §3.1.
 * Ключ — msg_id (8 байт). 64 записи, время жизни 30 секунд.
 * Запись добавляется ДО пересылки, а не после.
 */
#ifndef MESH_CORE_DEDUP_CACHE_H
#define MESH_CORE_DEDUP_CACHE_H

#include <stdint.h>
#include <stddef.h>
#include "msg_id.h"

namespace mesh {

class DedupCache {
public:
    static const int kCapacity = 64;
    static const uint32_t kTtlMs = 30000;

    DedupCache() { clear(); }

    void clear() {
        for (int i = 0; i < kCapacity; i++) { m_used[i] = false; m_age[i] = 0; }
        m_head = 0;
        m_count = 0;
    }

    /* Проверка: уже видели? По стороннему времени now_ms. */
    bool seen(const MsgId& id, uint32_t now_ms) const {
        uint8_t key[8];
        packMsgId(id, key);
        for (int i = 0; i < kCapacity; i++) {
            if (!m_used[i]) continue;
            if (expired(i, now_ms)) continue;
            if (memcmp(m_keys[i], key, 8) == 0) return true;
        }
        return false;
    }

    /* Добавить запись. Если уже есть — обновить время. */
    void remember(const MsgId& id, uint32_t now_ms) {
        uint8_t key[8];
        packMsgId(id, key);
        for (int i = 0; i < kCapacity; i++) {
            if (m_used[i] && !expired(i, now_ms) &&
                memcmp(m_keys[i], key, 8) == 0) {
                m_age[i] = now_ms;
                return;
            }
        }
        int slot = m_head;
        m_head = (m_head + 1) % kCapacity;
        if (!m_used[slot]) m_count++;
        memcpy(m_keys[slot], key, 8);
        m_age[slot] = now_ms;
        m_used[slot] = true;
    }

    /* Убрать протухшие записи (вызывается раз в секунду). */
    void sweep(uint32_t now_ms) {
        for (int i = 0; i < kCapacity; i++) {
            if (m_used[i] && expired(i, now_ms)) m_used[i] = false;
        }
    }

    int count() const { return m_count; }

    /* Последние использованные слоты — для тестов */
    int slotCount() const { return kCapacity; }

private:
    bool expired(int i, uint32_t now_ms) const {
        /* оборот millis() безопасен: unsigned разница */
        return (uint32_t)(now_ms - m_age[i]) > kTtlMs;
    }

    uint8_t m_keys[kCapacity][8];
    uint32_t m_age[kCapacity];
    bool     m_used[kCapacity];
    int      m_head;
    int      m_count;
};

} // namespace mesh

#endif /* MESH_CORE_DEDUP_CACHE_H */
