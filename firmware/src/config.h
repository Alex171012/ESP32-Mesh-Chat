/* config.h — пины, тайминги, лимиты. MESH_PROTOCOL, ARCHITECTURE §4. */
#ifndef MESH_CONFIG_H
#define MESH_CONFIG_H

/* Радио */
#define MESH_ESPNOW_CHANNEL     1
#define MESH_ESPNOW_SSID        "mesh"
#define MESH_ESPNOW_PASS        ""

/* Тайминги (MESH_PROTOCOL §5, ARCHITECTURE §4) */
#define MESH_HELLO_PERIOD_MS    10000
#define MESH_ONLINE_TIMEOUT_MS  30000
#define MESH_ACK_TIMEOUT_MS     5000
#define MESH_DEDUP_SWEEP_MS     1000
#define MESH_NODE_SWEEP_MS      5000

/* TTL по умолчанию (MESH_PROTOCOL §4) */
#define MESH_DEFAULT_TTL        4

/* LED — проверено на H1: GPIO8, это RGB-диод (PIN_RGB_LED в
 * variants/esp32c3/pins_arduino.h). digitalWrite(8, …) мигает видимо.
 * [ПРОВЕРИТЬ] поведение RGB-контроллера при ID_CONFLICT — мигание может
 * потребовать rgbLedWrite() вместо digitalWrite(). */
#define MESH_LED_PIN            8

/* BLE GATT (BLE_API §2) */
#define MESH_BLE_NAME_PREFIX    "MeshGate-"
#define MESH_SERVICE_UUID       "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define MESH_RX_UUID            "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define MESH_TX_UUID            "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

/* Кадрирование (BLE_API §3) */
#define MESH_FRAME_SOF          0x7E
#define MESH_FRAME_MAX          4096

/* Лимиты (BLE_API §3.2, §4.1) */
#define MESH_TEXT_LIMIT         179
#define MESH_NAME_LIMIT         24

#endif /* MESH_CONFIG_H */
