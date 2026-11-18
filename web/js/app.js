/* app.js — точка входа: выбор транспорта, события, отправка */

var app = (function () {
  "use strict";

  var transport = CONFIG.useMock ? mock : ble;
  window.transport = transport;

  var localSeq = 0;
  var reconnectTimer = null;
  var reconnectAttempt = 0;
  var userWantsConnection = false;
  var nameDialogShown = false;
  var lastPingAt = 0;
  var pingTimer = null;

  /* ================= Подключение ================= */

  function connect() {
    userWantsConnection = true;
    reconnectAttempt = 0;
    state.setConnecting(true);
    transport.connect();
  }

  function disconnect() {
    userWantsConnection = false;
    clearTimeout(reconnectTimer);
    transport.disconnect("user");
  }

  function scheduleReconnect() {
    if (!userWantsConnection) return;
    var delays = CONFIG.RECONNECT_DELAYS_MS;
    var d = delays[Math.min(reconnectAttempt, delays.length - 1)];
    reconnectAttempt++;
    state.setConnecting(true);
    var hint = document.getElementById("composer-hint");
    hint.textContent = "Переподключение через " + Math.round(d / 1000) + " с…";
    reconnectTimer = setTimeout(function () {
      hint.textContent = "";
      transport.connect();
    }, d);
  }

  /* ================= Статусы сообщений ================= */

  function handleStatus(st) {
    if (st.kind === "ready") { statusUI.setMtu(st.mtu); return; }
    if (st.kind === "tick") { statusUI.tickUptime(); return; }
    if (st.kind === "pong") return;

    /* локальный id при отправке → id в транспорте: ищем по обоим */
    var m = findByAnyId(st.id);
    if (!m) return;

    var patch = { status: st.state };
    if (st.state === "delivered") {
      if (st.path) patch.path = st.path;
      if (st.hops != null) patch.hops = st.hops;
      if (st.rtt_ms != null) patch.rtt = st.rtt_ms;
      state.setStats({ delivered: state.S.stats.delivered + 1 });
    }
    if (st.state === "failed") {
      state.setStats({ failed: state.S.stats.failed + 1 });
    }
    state.updateMessage(m.id, patch);
  }

  function findByAnyId(id) {
    var msgs = state.S.messages;
    for (var i = 0; i < msgs.length; i++) {
      if (msgs[i].id === id || msgs[i].transportId === id) return msgs[i];
    }
    return null;
  }

  /* ================= Отправка ================= */

  function sendMessage() {
    var input = document.getElementById("input");
    var text = input.value.replace(/^\s+|\s+$/g, "");
    if (!text) return;
    if (!state.S.connected) return;

    if (utf8Length(text) > CONFIG.TEXT_LIMIT_BYTES) {
      statusUI.showErr("text_too_long");
      return;
    }

    var dst = state.S.dst || 0;
    var id = "local-" + (++localSeq) + "-" + Date.now().toString(36);

    var msg = {
      id: id,
      transportId: null,
      out: true,
      dst: dst,
      dstName: dst ? nodeName(dst) : null,
      text: text,
      path: null,
      status: "accepted",
      localTs: Date.now(),
      onRetry: function () {
        state.updateMessage(id, { status: "accepted", path: null });
        sendTransport(dst, text, id);
      }
    };

    state.addMessage(msg);
    state.setStats({ sent: state.S.stats.sent + 1 });
    input.value = "";
    statusUI.updateCounter();

    sendTransport(dst, text, id);
  }

  function sendTransport(dst, text, localId) {
    var tid = transport.send({ dst: dst, text: text, ack: true });
    var m = findByAnyId(localId);
    if (m && typeof tid === "string") m.transportId = tid;
  }

  function nodeName(id) {
    for (var i = 0; i < state.S.nodes.length; i++) {
      var n = state.S.nodes[i];
      if (n.id === id) return n.name || ("Узел " + route.hex(id));
    }
    return route.hex(id);
  }

  /* ================= Имя узла ================= */

  function maybeAskName() {
    if (nameDialogShown) return;
    if (!state.S.connected) return;
    if (state.S.nameAssigned) return;
    nameDialogShown = true;
    document.getElementById("name-modal").hidden = false;
    document.getElementById("modal-name-input").focus();
  }

  function initNameDialog() {
    var modal = document.getElementById("name-modal");
    var input = document.getElementById("modal-name-input");
    var counter = document.getElementById("modal-name-counter");
    var save = document.getElementById("modal-name-save");
    var skip = document.getElementById("modal-name-skip");

    function upd() {
      var n = utf8Length(input.value);
      counter.textContent = n + " / " + CONFIG.NAME_LIMIT_BYTES + " байт";
      if (n > CONFIG.NAME_LIMIT_BYTES) counter.classList.add("over");
      else counter.classList.remove("over");
      save.disabled = (n === 0 || n > CONFIG.NAME_LIMIT_BYTES);
    }

    input.addEventListener("input", upd);
    save.addEventListener("click", function () {
      var v = input.value.replace(/^\s+|\s+$/g, "");
      if (!v) return;
      transport.setName(v);
      modal.hidden = true;
    });
    skip.addEventListener("click", function () { modal.hidden = true; });
    input.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && !save.disabled) save.click();
    });
    upd();
  }

  /* ================= События транспорта ================= */

  function bindTransport() {
    transport.on("ble", function (ev) {
      clearTimeout(reconnectTimer);

      if (ev.connecting) {
        state.setConnecting(true);
        statusUI.renderConnection();
        return;
      }

      var wasConnected = state.S.connected;
      state.setConnected(ev.connected, ev.gateway);
      statusUI.renderConnection();

      if (ev.connected) {
        reconnectAttempt = 0;
        transport.getNodes();
        transport.getName();

        if (pingTimer) clearInterval(pingTimer);
        pingTimer = setInterval(function () {
          if (!state.S.connected) return;
          lastPingAt = performance.now();
          transport.ping();
        }, 5000);
        lastPingAt = performance.now();
        transport.ping();

        statusUI.renderSelf();
        statusUI.renderName();
        if (!isDemoMode()) setTimeout(maybeAskName, 500);
      } else {
        statusUI.setPing(null);
        if (pingTimer) { clearInterval(pingTimer); pingTimer = null; }
        /* разрыв не по воле пользователя → автопереподключение (BLE_API §6.2) */
        if (wasConnected && ev.reason !== "user") scheduleReconnect();
      }
    });

    transport.on("msg", function (m) {
      var msg = {};
      Object.keys(m).forEach(function (k) { msg[k] = m[k]; });
      msg.out = false;
      msg.status = null;
      msg.localTs = Date.now();
      state.addMessage(msg);
    });

    transport.on("status", handleStatus);

    transport.on("nodes", function (list) {
      state.setNodes(list);
      nodesUI.renderAll(list);
    });

    transport.on("name", function (ev) {
      state.setName(ev.name, ev.assigned);
      statusUI.renderName();
      if (ev.assigned) document.getElementById("name-modal").hidden = true;
    });

    transport.on("pong", function () {
      if (lastPingAt) statusUI.setPing(Math.round(performance.now() - lastPingAt));
    });

    transport.on("err", function (ev) {
      statusUI.showErr(ev.code, ev.msg);
    });
  }

  /* ================= Инициализация ================= */

  /* Демонстрация интерфейса: ?demo=1 наполняет ленту примерами
     (маршрут, статусы) — для проверки без действий пользователя */
  function seedDemo() {
    if (!isDemoMode()) return;
    var demoMsgs = [
      { out: false, src: 4660, src_name: "Узел 3", dst: 0,
        text: "Всем привет из mesh-сети", path: [0x5678, 22136],
        status: null, localTs: Date.now() - 48000 },
      { out: true, dst: 0, dstName: null, text: "Общая рассылка узлам 2 и 3",
        path: [22136, 4660], status: "delivered",
        rtt: 187, localTs: Date.now() - 40000 },
      { out: true, dst: 4660, dstName: "Узел 3", text: "Проверка связи: 1 → 2 → 3",
        path: [22136, 4660], status: "delivered",
        rtt: 164, localTs: Date.now() - 30000 },
      { out: false, src: 22136, src_name: "Узел 2", dst: 0,
        text: "Слышу вас, ретранслирую дальше", path: [0x5678],
        status: null, localTs: Date.now() - 22000 },
      { out: true, dst: 4660, dstName: "Узел 3", text: "Сообщение с потерей пакета",
        path: null, status: "failed", localTs: Date.now() - 15000 },
      { out: true, dst: 0, dstName: null, text: "Набросок, который не ушёл — разрыв связи",
        path: null, status: "unsent", localTs: Date.now() - 8000 }
    ];
    demoMsgs.forEach(function (m) { state.addMessage(m); });
    state.setStats({ sent: 4, delivered: 3, failed: 2 });
    statusUI.renderStats();
  }

  function isDemoMode() {
    return new URLSearchParams(location.search).get("demo") === "1";
  }

  function init() {
    chat.init();
    nodesUI.init();
    statusUI.init();
    initNameDialog();
    bindTransport();

    var input = document.getElementById("input");
    var sendBtn = document.getElementById("send");

    sendBtn.addEventListener("click", sendMessage);
    input.addEventListener("input", statusUI.updateCounter);
    input.addEventListener("keydown", function (e) {
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault();
        sendMessage();
      }
    });

    /* мобильная панель узлов */
    document.getElementById("drawer-open").addEventListener("click", function () {
      document.querySelector(".col-nodes").classList.add("open");
    });
    document.getElementById("drawer-close").addEventListener("click", function () {
      document.querySelector(".col-nodes").classList.remove("open");
    });

    /* события состояния → лента */
    state.on("msg:add", function (m) { chat.add(m); });
    state.on("msg:update", function (m) { chat.update(m); });
    state.on("stats", function () { statusUI.renderStats(); });

    if (CONFIG.useMock) document.getElementById("badge-mock").hidden = false;

    /* демо-режим: закрыть диалог имени и наполнить ленту примерами */
    if (isDemoMode()) {
      document.getElementById("name-modal").hidden = true;
      seedDemo();
    }

    statusUI.updateCounter();
    statusUI.renderConnection();
    statusUI.renderName();

    /* автоподключение на заглушке, чтобы страница была живой сразу */
    if (CONFIG.useMock) setTimeout(connect, 300);
  }

  document.addEventListener("DOMContentLoaded", init);

  return { connect: connect, disconnect: disconnect };
})();