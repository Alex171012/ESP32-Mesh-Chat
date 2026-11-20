/* ble.js — реальный Web Bluetooth (этап 9).
 *
 * Контракт транспорта — WEB_CLIENT_PLAN §4, зеркалирует mock.js:
 *   команды: connect, disconnect, send, getNodes, getName, setName,
 *            clearName, ping
 *   события: ble, status, msg, nodes, name, pong, err
 *
 * GATT — BLE_API §2 (Nordic UART Service): клиент пишет в RX
 * (write/write_nr), читает из TX (notify). Кадрирование и JSON —
 * BLE_API §3–4 через protocol.js (encode/decode/createFeeder).
 *
 * Ограничения (BLE_API §1):
 *  - requestDevice() ТОЛЬКО из обработчика клика — автоподключение
 *    при загрузке страницы невозможно.
 *  - Нужен secure context: http://localhost или HTTPS, не file://.
 */

var ble = (function () {
  "use strict";

  var handlers = {};

  /* ---- Состояние подключения ---- */
  var device = null;          /* BluetoothDevice — переживает переподключение */
  var server = null;
  var rxChar = null;          /* клиент → шлюз (write) */
  var txChar = null;          /* шлюз → клиент (notify) */
  var connected = false;
  var feeder = null;          /* сборщик кадров из потока notify */
  var writeChain = Promise.resolve();  /* очередь writeValueWithoutResponse */
  var tickTimer = null;       /* счётчик «Время сессии», 1 раз в секунду */

  /* Привязка status-событий к сообщениям в ленте.
   * Шлюз на «send» шлёт id:"local" (accepted), дальше — hex msg_id, который
   * клиент заранее не знает. app.js ищет сообщение по transportId, поэтому
   * клиент назначает свой transportId и держит его «ожидающим», пока статусы
   * идут с id:"local". Как только пришёл свой id — дальше шлюз шлёт свой же. */
  var pendingSendId = null;

  function mapStatusId(gatewayId) {
    if (gatewayId == null || gatewayId === "local") {
      return pendingSendId || gatewayId;
    }
    return gatewayId;
  }

  /* ---- Вспомогательные ---- */
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

  function err(code, msg) {
    fire("err", { code: code, msg: msg });
    console.warn("[ble]", code, msg || "");
  }

  /* Отправить кадр в шлюз. Очередь — writeValue без ожидания ответа
   * стека, но строго последовательно (иначе кадры перемешаются). */
  function sendFrame(obj) {
    if (!rxChar || !connected) return false;
    var frame = protocol.encode(obj);
    writeChain = writeChain.then(function () {
      if (!rxChar || !connected) return;
      return rxChar.writeValueWithoutResponse(frame).catch(function (e) {
        err("write_failed", String(e && e.message ? e.message : e));
      });
    });
    return true;
  }

  /* ---- Приём: сырой поток notify → кадры → JSON ---- */
  function handleFrames(frames) {
    for (var i = 0; i < frames.length; i++) {
      var o = frames[i];
      if (!o || !o.t) continue;

      switch (o.t) {
        case "msg":
          /* входящее сообщение → лента (app.js копирует поля) */
          fire("msg", o);
          break;

        case "status":
          /* шлюз → клиент: state = accepted/sent/delivered/failed.
           * id у шлюза: "local" (accepted) или hex msg_id (sent и дальше).
           * Сопоставляем с последним отправленным клиентом сообщением. */
          if (o.kind === "ready") {
            /* Готовность шлюза после подписки на notify / обмена MTU:
             * app.js (handleStatus) показывает MTU в панели состояния. */
            fire("status", { kind: "ready", mtu: o.mtu });
            break;
          }
          fire("status", {
            id: mapStatusId(o.id),
            state: o.state,
            path: o.path,
            hops: o.hops,
            rtt_ms: o.rtt_ms
          });
          break;

        case "nodes":
          fire("nodes", o.list || []);
          break;

        case "name":
          fire("name", { name: (o.name === undefined ? null : o.name),
                         assigned: !!o.assigned });
          break;

        case "pong":
          fire("pong", { id: o.id, rtt_ms: o.rtt_ms });
          break;

        case "ble":
          /* шлюз сообщает состояние: {t:"ble", connected, gateway} */
          if (o.connected && o.gateway != null) {
            fire("ble", { connected: true, gateway: o.gateway });
          } else if (!o.connected) {
            fire("ble", { connected: false });
          }
          break;

        case "err":
          err(o.code || "err", o.msg);
          break;

        default:
          console.warn("[ble] неизвестный тип кадра:", o.t, o);
      }
    }
  }

  /* ---- Подключение ---- */

  function connect() {
    if (connected) return;
    if (!navigator.bluetooth) {
      err("no_web_bluetooth",
          "Web Bluetooth недоступен. Нужен Chrome/Edge и http://localhost");
      return;
    }
    if (!device) {
      /* Первое подключение: жест пользователя обязателен (BLE_API §1). */
      fire("ble", { connecting: true });
      navigator.bluetooth.requestDevice({
        filters: [{ namePrefix: "MeshGate-" }],
        optionalServices: [CONFIG.SERVICE_UUID]
      }).then(function (d) {
        device = d;
        device.addEventListener("gattserverdisconnected", onDisconnected);
        return doConnect();
      }).catch(function (e) {
        if (e && e.name === "NotFoundError") {
          /* пользователь закрыл диалог — не ошибка */
          fire("ble", { connected: false, reason: "user" });
        } else {
          err("request_failed", String(e && e.message ? e.message : e));
          fire("ble", { connected: false, reason: "user" });
        }
      });
      return;
    }
    /* Переподключение к уже выбранному устройству (BLE_API §6.2) */
    fire("ble", { connecting: true });
    doConnect();
  }

  function doConnect() {
    if (!device || !device.gatt) {
      err("no_device", "Устройство не выбрано");
      fire("ble", { connected: false });
      return Promise.resolve();
    }
    return device.gatt.connect().then(function (s) {
      server = s;
      return server.getPrimaryService(CONFIG.SERVICE_UUID);
    }).then(function (svc) {
      return Promise.all([
        svc.getCharacteristic(CONFIG.RX_UUID),
        svc.getCharacteristic(CONFIG.TX_UUID)
      ]);
    }).then(function (chars) {
      rxChar = chars[0];
      txChar = chars[1];

      feeder = protocol.createFeeder(function (msg) {
        console.warn("[ble] кадр: " + msg);
      });

      return txChar.startNotifications().then(function (c) {
        c.addEventListener("characteristicvaluechanged", onNotify);
        connected = true;
        fire("ble", { connected: true, gateway: null });
        startTick();
        /* MTU приходит от шлюза: status {kind:"ready", mtu} после подписки
         * на notify и после обмена MTU (ble_gateway: onSubscribe/onMTUChange). */
      });
    }).catch(function (e) {
      connected = false;
      rxChar = txChar = server = null;
      err("connect_failed", String(e && e.message ? e.message : e));
      fire("ble", { connected: false, reason: "connect" });
    });
  }

  function onNotify(ev) {
    var v = ev.target.value;   /* DataView */
    if (!v) return;
    var bytes = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
    try {
      var frames = feeder(bytes);
      if (frames && frames.length) handleFrames(frames);
    } catch (e) {
      err("parse_failed", String(e && e.message ? e.message : e));
    }
  }

  function onDisconnected() {
    if (!connected) return;
    connected = false;
    rxChar = txChar = server = null;
    stopTick();
    /* Разрыв не по воле пользователя → app.js запланирует переподключение */
    fire("ble", { connected: false, reason: "disconnect" });
  }

  /* Тик для счётчика «Время сессии» (app.js → statusUI.tickUptime).
   * Шлюз его не шлёт — клиент генерирует сам, как mock.js. */
  function startTick() {
    stopTick();
    tickTimer = setInterval(function () {
      if (!connected) return;
      fire("status", { kind: "tick" });
    }, 1000);
  }
  function stopTick() {
    if (tickTimer) { clearInterval(tickTimer); tickTimer = null; }
  }

  function disconnect(reason) {
    if (device && device.gatt && device.gatt.connected) {
      try { device.gatt.disconnect(); } catch (e) { /* ignore */ }
    }
    var wasConnected = connected;
    connected = false;
    rxChar = txChar = server = null;
    stopTick();
    if (wasConnected || reason === "user") {
      fire("ble", { connected: false, reason: reason || "user" });
    }
  }

  /* ---- Команды ---- */

  function send(cmd) {
    if (!connected) {
      err("not_connected", "Нет подключения к шлюзу");
      return null;
    }
    /* Локальный transportId: app.js сохранит его и найдёт по нему status
     * со state:"accepted" (шлюз шлёт id:"local"). */
    var localId = "ble-" + Date.now().toString(36) + "-" +
                  Math.random().toString(36).slice(2, 7);
    pendingSendId = localId;

    sendFrame({
      t: "send",
      dst: cmd.dst || 0,
      text: cmd.text,
      ack: cmd.ack !== false
    });
    return localId;
  }

  function getNodes() {
    if (connected) sendFrame({ t: "nodes" });
  }

  function ping() {
    if (!connected) return;
    var id = (ping._seq = (ping._seq || 0) + 1);
    sendFrame({ t: "ping", id: id });
  }

  function getName() {
    if (connected) sendFrame({ t: "get_name" });
  }

  function setName(name) {
    if (connected) sendFrame({ t: "set_name", name: name });
    else err("not_connected", "Нет подключения к шлюзу");
  }

  function clearName() {
    if (connected) sendFrame({ t: "clear_name" });
    else err("not_connected", "Нет подключения к шлюзу");
  }

  return {
    isMock: false,
    on: on,
    connect: connect,
    disconnect: disconnect,
    send: send,
    getNodes: getNodes,
    ping: ping,
    getName: getName,
    setName: setName,
    clearName: clearName
  };
})();
