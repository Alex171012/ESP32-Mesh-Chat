/* ble.js — реальный Web Bluetooth (этап 9, после железа)
 * Пока плата не подключена, модуль заглушён: connect() сообщает об этом
 * событием err. Вызовы совпадают с контрактом mock.js (WEB_CLIENT_PLAN §4).
 */

var ble = (function () {
  "use strict";

  var handlers = {};

  function on(event, cb) {
    if (!handlers[event]) handlers[event] = [];
    handlers[event].push(cb);
  }

  function fire(event, payload) {
    var list = handlers[event] || [];
    for (var i = 0; i < list.length; i++) {
      try { list[i](payload); }
      catch (e) { console.error("[ble:" + event + "]", e); }
    }
  }

  function notReady() {
    fire("err", {
      code: "not_implemented",
      msg: "Реальный BLE доступен на этапе 9: нужна подключённая плата. Сейчас работает заглушка — откройте страницу с ?mock=1"
    });
  }

  return {
    isMock: false,
    on: on,
    connect: notReady,
    disconnect: function () { fire("ble", { connected: false, reason: "user" }); },
    send: notReady,
    getNodes: notReady,
    ping: notReady,
    getName: notReady,
    setName: notReady,
    clearName: notReady
  };
})();