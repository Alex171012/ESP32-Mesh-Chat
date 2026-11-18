/* config.js — константы клиента (BLE_API.md) */

var CONFIG = {
  /* GATT Nordic UART Service */
  SERVICE_UUID: "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
  RX_UUID: "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
  TX_UUID: "6e400003-b5a3-f393-e0a9-e50e24dcca9e",

  /* Кадрирование */
  SOF: 0x7e,

  /* Лимиты (MESH_PROTOCOL §2.5, BLE_API §3.2, §4.1) */
  TEXT_LIMIT_BYTES: 179,   /* байт UTF-8 под текст */
  NAME_LIMIT_BYTES: 24,    /* байт UTF-8 имя узла */

  /* Тайм-ауты */
  ACK_TIMEOUT_MS: 5000,
  PING_TIMEOUT_MS: 3000,
  RECONNECT_DELAYS_MS: [1000, 2000, 4000, 8000, 16000],
  RECONNECT_MAX_DELAY_MS: 30000,
  UPTIME_TICK_MS: 1000,

  /* Режим транспорта. Пока железа нет — заглушка по умолчанию;
     реальный BLE включается параметром ?mock=0 */
  useMock: new URLSearchParams(location.search).get("mock") !== "0"
};

/* Точный размер строки в байтах UTF-8 */
function utf8Length(str) {
  return new TextEncoder().encode(str).length;
}