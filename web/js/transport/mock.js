/* mock.js — заглушка транспорта (BLE_API §5, WEB_CLIENT_PLAN §4)
 * Воспроизводит сценарий демонстрации: 3 узла, задержки, потери,
 * маршрут 1 → 2 → 3, статусы accepted→sent→delivered/failed,
 * периодические HELLO и уход узла 3 в offline.
 * Не обращается к navigator.bluetooth — работает с file://.
 */

var mock = (function () {
  "use strict";

  var GATEWAY_ID = 0x5678;     /* «наш» узел (шлюз) */
  var NODE2_ID = 22136;        /* узел 2 — прямой сосед */
  var NODE3_ID = 4660;         /* узел 3 — достижим через узел 2 */
  var SELF_SESSION = 0x002a;

  var DELAY = 150;             /* базовая задержка доставки, мс */
  var LOSS_EVERY = 10;         /* каждый 10-й пакет теряется */

  var handlers = {};           /* event -> [cb] */
  var timers = [];
  var seq = 7;
  var sendCount = 0;
  var lossRate = 0.1;
  var nextPingId = 1;
  var tick = null;
  var name = null;             /* имя в «NVS» заглушки */
  var nameWasSet = false;

  var now0 = Date.now();
  function uptime() { return Date.now() - now0; }

  /* ---- Неблокирующие таймеры с очисткой ---- */
  function later(ms, fn) {
    var t = setTimeout(function () {
      timers = timers.filter(function (x) { return x !== t; });
      fn();
    }, ms);
    timers.push(t);
    return t;
  }

  function clearTimers() {
    for (var i = 0; i < timers.length; i++) clearTimeout(timers[i]);
    timers = [];
    if (tick) { clearInterval(tick); tick = null; }
  }

  function fire(event, payload) {
    var list = handlers[event] || [];
    for (var i = 0; i < list.length; i++) {
      try { list[i](payload); }
      catch (e) { console.error("[mock:" + event + "]", e); }
    }
  }

  function clone(n) {
    var o = {};
    Object.keys(n).forEach(function (k) { o[k] = n[k]; });
    return o;
  }

  /* ---- Сеть ---- */
  var network = {
    nodes: [
      { id: NODE2_ID, name: "Узел 2", neighbor: true,  online: true, rssi: -55, last_seen_ms: 1200 },
      { id: NODE3_ID, name: "Узел 3", neighbor: false, online: true, rssi: -71, last_seen_ms: 8400 }
    ]
  };

  function nodesSnapshot() {
    return network.nodes.map(clone);
  }

  function msgId() {
    seq = (seq + 1) >>> 0;
    function pad(v, n) {
      var s = v.toString(16);
      while (s.length < n) s = "0" + s;
      return s;
    }
    return pad(GATEWAY_ID, 4) + pad(SELF_SESSION, 4) + pad(seq, 8);
  }

  function rollLoss() {
    sendCount++;
    if (Math.random() < lossRate) return true;
    return sendCount % LOSS_EVERY === 0;
  }

  function pathFor(dst) {
    /* демонстрация: узел 1 идёт к узлу 3 через узел 2, к узлу 2 — напрямую.
       path — пройденные узлы (без отправителя и получателя), MESH_PROTOCOL §6.1 */
    if (dst === NODE3_ID) return [GATEWAY_ID, NODE2_ID];
    if (dst === NODE2_ID) return [];
    return [GATEWAY_ID, NODE2_ID];
  }

  /* ---- Транспорт ---- */

  function connect() {
    clearTimers();
    fire("ble", { connected: false, connecting: true, gateway: GATEWAY_ID });

    later(260, function () {
      state.setSelf(GATEWAY_ID, SELF_SESSION);
      fire("ble", { connected: true, gateway: GATEWAY_ID, mtu: 247 });

      /* тик для счётчиков */
      tick = setInterval(function () {
        fire("status", { kind: "tick", ts: uptime() });
      }, 1000);

      /* HELLO обновляет список узлов */
      later(700, function () { fire("nodes", nodesSnapshot()); });

      /* узел 3 уходит в offline примерно через минуту и возвращается */
      later(60000, function () { setNode3(false); });
      later(95000, function () { setNode3(true); });
      later(150000, function () { setNode3(false); });

      /* имя из «NVS»: в демо-режиме сразу задано, чтобы не показывался диалог */
      later(400, function () {
        var demo = new URLSearchParams(location.search).get("demo") === "1";
        if (demo) {
          if (!nameWasSet) name = "Ноутбук";
          nameWasSet = true;
        }
        fire("name", { name: nameWasSet ? name : null, assigned: nameWasSet });
      });
    });
  }

  function setNode3(online) {
    for (var i = 0; i < network.nodes.length; i++) {
      if (network.nodes[i].id === NODE3_ID) {
        network.nodes[i].online = online;
        network.nodes[i].last_seen_ms = online ? 0 : 61000;
      }
    }
    fire("nodes", nodesSnapshot());
  }

  function disconnect(reason) {
    clearTimers();
    fire("ble", { connected: false, gateway: GATEWAY_ID, reason: reason || "user" });
  }

  function send(cmd) {
    var id = msgId();
    var dst = cmd.dst || 0;
    var path = pathFor(dst);

    /* 1. принято шлюзом — сразу после записи в характеристику */
    fire("status", { id: id, state: "accepted", dst: dst });

    var willDrop = rollLoss();

    /* 2. передано в эфир — отдельный момент времени */
    later(DELAY / 3, function () {
      fire("status", { id: id, state: "sent", dst: dst });
    });

    /* 3. доставлено / потеряно — с задержкой, не в одном тике */
    later(DELAY + Math.random() * 120, function () {
      if (willDrop) {
        fire("status", { id: id, state: "failed", dst: dst, reason: "timeout" });
      } else {
        fire("status", {
          id: id, state: "delivered", dst: dst,
          path: path, hops: path.length,
          rtt_ms: DELAY + Math.round(Math.random() * 80)
        });
        /* ответ получателя — только для адресных сообщений */
        if (dst !== 0) {
          later(200, function () {
            var src = null;
            for (var i = 0; i < network.nodes.length; i++) {
              if (network.nodes[i].id === dst) src = network.nodes[i];
            }
            fire("msg", {
              id: msgId(),
              src: dst,
              src_name: src ? src.name : null,
              dst: 0,
              text: "принял",
              path: path,
              hops: path.length,
              rssi: src ? src.rssi : -60,
              ts: uptime()
            });
          });
        }
      }
    });

    return id;
  }

  function getNodes() {
    later(80, function () { fire("nodes", nodesSnapshot()); });
  }

  function ping() {
    var id = nextPingId++;
    later(DELAY + 30, function () { fire("pong", { id: id, rtt_ms: DELAY + 30 }); });
    return id;
  }

  function getName() {
    later(60, function () {
      fire("name", { name: nameWasSet ? name : null, assigned: nameWasSet });
    });
  }

  function setName(value) {
    name = value;
    nameWasSet = true;
    later(60, function () { fire("name", { name: value, assigned: true }); });
  }

  function clearName() {
    name = null;
    nameWasSet = false;
    later(60, function () { fire("name", { name: null, assigned: false }); });
  }

  /* ---- Тестовые рычаги для отладки интерфейса ---- */
  function findNode(id) {
    for (var i = 0; i < network.nodes.length; i++) {
      if (network.nodes[i].id === id) return network.nodes[i];
    }
    return null;
  }
  function forceOffline(id) {
    var n = findNode(id);
    if (n) { n.online = false; n.last_seen_ms = 61000; fire("nodes", nodesSnapshot()); }
  }
  function forceOnline(id) {
    var n = findNode(id);
    if (n) { n.online = true; n.last_seen_ms = 0; fire("nodes", nodesSnapshot()); }
  }
  function dropNext() { sendCount = LOSS_EVERY - 1; }   /* следующий send потеряем */
  function setLoss(v) { lossRate = Math.max(0, Math.min(1, v)); }
  function forceDisconnect() { disconnect("mock"); }
  function forceBusy() {
    fire("err", { code: "busy", msg: "Узел занят другим подключением" });
  }

  function on(event, cb) {
    if (!handlers[event]) handlers[event] = [];
    handlers[event].push(cb);
  }

  return {
    isMock: true,
    on: on, connect: connect, disconnect: disconnect, send: send,
    getNodes: getNodes, ping: ping,
    getName: getName, setName: setName, clearName: clearName,
    forceOffline: forceOffline, forceOnline: forceOnline,
    dropNext: dropNext, setLoss: setLoss,
    forceDisconnect: forceDisconnect, forceBusy: forceBusy,
    GATEWAY_ID: GATEWAY_ID, NODE2_ID: NODE2_ID, NODE3_ID: NODE3_ID
  };
})();