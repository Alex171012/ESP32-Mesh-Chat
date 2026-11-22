/* ble_gateway.h — GATT-сервер, кадрирование, JSON-команды.
 * ARCHITECTURE §2, BLE_API §3–4. Отвечает за обмен с веб-клиентом.
 */
#ifndef MESH_BLE_GATEWAY_H
#define MESH_BLE_GATEWAY_H

#include <stdint.h>
#include <stddef.h>

/* Команды, пришедшие от клиента (разобранные JSON) */
enum BleCmdType : uint8_t {
    BLE_CMD_NONE = 0,
    BLE_CMD_SEND,       /* dst, text, ack */
    BLE_CMD_NODES,
    BLE_CMD_PING,       /* id */
    BLE_CMD_GET_NAME,
    BLE_CMD_SET_NAME,   /* name */
    BLE_CMD_CLEAR_NAME
};

struct BleCommand {
    BleCmdType type;
    uint16_t   dst;
    char       text[180];
    char       name[25];
    uint32_t   ping_id;
    bool       ack;
};

/* Колбэки, которые main.cpp подключает к своей логике */
typedef void (*BleCmdCb)(const BleCommand& cmd);
typedef void (*BleEventCb)(const char* json);   /* готовый JSON клиенту */

/* Инициализация BLE-сервера. Возвращает false, если NimBLE не поднялся. */
bool ble_gateway_init(const char* device_name, BleCmdCb on_cmd, BleEventCb on_event);

/* Проверить, подключён ли клиент */
bool ble_gateway_connected();

/* Отправить JSON клиенту (не подключён — молча отбрасываем) */
void ble_gateway_send_json(const char* json);

/* Экранировать строку для вставки внутрь JSON-строкового литерала:
 * кавычки и обратный слэш — с обратным слэшем, управляющие (<0x20) — пробел.
 * main.cpp вызывает при сборке кадров с текстом/именами: без этого `"`
 * в тексте сообщения ломает JSON всего кадра. */
void ble_gateway_json_escape(const char* in, char* out, size_t cap);

/* Отправить кадр [SOF|len|payload|crc16] клиенту (вызывается из send_json) */
void ble_gateway_send_frame(const uint8_t* payload, size_t len);

/* Периодическое обслуживание (вызывать из loop). Тяжёлая работа шлюза —
 * здесь, на стеке loopTask: коллбэки NimBLE выполняются в задаче
 * nimble_host (~5 КБ стека) и только ставят флаги/пишут в кольцо байтов. */
void ble_gateway_poll();

/* Отключить клиента (по команде busy) */
void ble_gateway_disconnect_client();

#endif /* MESH_BLE_GATEWAY_H */
