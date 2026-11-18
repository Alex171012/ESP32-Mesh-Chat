/* espnow_link.h — радио ESP-NOW: MAC, отправка, приём.
 * ARCHITECTURE §2. Единственный модуль, знающий esp_now.h.
 */
#ifndef MESH_ESPNOW_LINK_H
#define MESH_ESPNOW_LINK_H

#include <stdint.h>
#include <stddef.h>

/* Колбэк приёма: байты пакета + MAC отправителя + RSSI */
typedef void (*EspNowRecvCb)(const uint8_t* data, size_t len,
                             const uint8_t* src_mac, int8_t rssi);
/* Колбэк результата отправки */
typedef void (*EspNowSendCb)(const uint8_t* mac, int status);

/* Инициализация: WiFi in STATION mode + ESP-NOW. */
bool espnow_link_init(EspNowRecvCb on_recv, EspNowSendCb on_send);

/* Обновление MAC из node_id (последние 2 байта efuse-MAC).
 * Для широковещания используется broadcast MAC FF:FF:FF:FF:FF:FF.
 */
void espnow_link_set_self(const uint8_t mac[6]);

/* Отправить пакет конкретному узлу (по MAC) или broadcast.
 * Возвращает true, если отправка принята стеком.
 */
bool espnow_link_send(const uint8_t* mac, const uint8_t* data, size_t len);

/* Отправить broadcast */
bool espnow_link_send_broadcast(const uint8_t* data, size_t len);

/* Обновить peer-таблицу (вызывать при добавлении известного узла) */
void espnow_link_add_peer(const uint8_t mac[6]);

/* Деинициализация */
void espnow_link_deinit();

#endif /* MESH_ESPNOW_LINK_H */
