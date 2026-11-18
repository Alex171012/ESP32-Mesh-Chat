/* status.js — состояние BLE, счётчик байт, ошибки, имя узла */

var statusUI = (function () {
  "use strict";

  var dot, stateEl, subEl, connectBtn, counter, hint, input, sendBtn;
  var nameBtn, clearNameBtn, nameEdit, nameInput, nameCounter, nameSave;
  var statsBlock, pingEl, mtuEl, uptimeEl;
  var lastPing = null;
  var uptimeStart = null;

  function $(id) { return document.getElementById(id); }

  function init() {
    dot = $("conn-dot");
    stateEl = $("conn-state");
    subEl = $("conn-sub");
    connectBtn = $("connect-btn");
    counter = $("counter");
    hint = $("composer-hint");
    input = $("input");
    sendBtn = $("send");
    statsBlock = $("conn-stats");
    pingEl = $("stat-ping");
    mtuEl = $("stat-mtu");
    uptimeEl = $("stat-uptime");

    nameBtn = $("name-btn");
    clearNameBtn = $("clear-name-btn");
    nameEdit = $("name-edit");
    nameInput = $("name-input");
    nameCounter = $("name-counter");
    nameSave = $("name-save");

    connectBtn.addEventListener("click", function () {
      if (state.S.connected) app.disconnect();
      else app.connect();
    });

    nameBtn.addEventListener("click", function () {
      nameEdit.hidden = !nameEdit.hidden;
      if (!nameEdit.hidden) {
        nameInput.value = state.S.gatewayName || "";
        updateNameCounter();
        nameInput.focus();
      }
    });

    nameSave.addEventListener("click", saveName);
    nameInput.addEventListener("keydown", function (e) {
      if (e.key === "Enter") saveName();
    });
    nameInput.addEventListener("input", updateNameCounter);

    clearNameBtn.addEventListener("click", function () {
      if (window.confirm("Сбросить имя узла? Оно стёрется на плате.")) {
        transport.clearName();
      }
    });

    updateNameCounter();
    nameBtn.disabled = true;
    clearNameBtn.disabled = true;
    renderConnection();
  }

  function saveName() {
    var v = nameInput.value.replace(/^\s+|\s+$/g, "");
    if (!v) return;
    if (utf8Length(v) > CONFIG.NAME_LIMIT_BYTES) {
      showErr("Имя слишком длинное: не более " + CONFIG.NAME_LIMIT_BYTES +
              " байт (русская буква — 2 байта)");
      return;
    }
    transport.setName(v);
    nameEdit.hidden = true;
  }

  function updateNameCounter() {
    var n = utf8Length(nameInput.value);
    nameCounter.textContent = n + " / " + CONFIG.NAME_LIMIT_BYTES + " байт";
    if (n > CONFIG.NAME_LIMIT_BYTES) nameCounter.classList.add("over");
    else nameCounter.classList.remove("over");
  }

  /* ---- Счётчик текста: байты, не символы ---- */
  function updateCounter() {
    var n = utf8Length(input.value);
    var limit = CONFIG.TEXT_LIMIT_BYTES;
    counter.textContent = n + " / " + limit + " байт";
    if (n > limit) counter.classList.add("over");
    else counter.classList.remove("over");

    if (n > limit) {
      hint.textContent = "Слишком длинное сообщение — лимит " + limit + " байт";
      hint.style.color = "";
      sendBtn.disabled = true;
    } else {
      if (hint.style.color !== "rgb(248, 81, 73)") hint.textContent = "";
      sendBtn.disabled = !state.S.connected || !input.value.replace(/^\s+|\s+$/g, "");
    }

    /* автовысота поля */
    input.style.height = "auto";
    input.style.height = Math.min(input.scrollHeight, 140) + "px";
  }

  function renderConnection() {
    var c = state.S.connected;
    var connecting = state.S.connecting;
    dot.className = "dot " + (c ? "on" : (connecting ? "connecting" : "off"));
    stateEl.textContent = c ? "Подключено" : (connecting ? "Подключение…" : "Нет связи");

    if (c) {
      var gw = state.S.gateway;
      subEl.textContent = "шлюз " + route.hex(gw) +
        (state.S.gatewayName ? " · " + state.S.gatewayName : "");
      connectBtn.textContent = "Отключиться";
      connectBtn.className = "btn btn-accent-quiet";
      statsBlock.hidden = false;
      if (!uptimeStart) uptimeStart = Date.now();
    } else {
      subEl.textContent = connecting ? "ищем шлюз…" : "шлюз не подключён";
      connectBtn.textContent = "Подключиться";
      connectBtn.className = "btn btn-primary";
      statsBlock.hidden = true;
      uptimeStart = null;
    }
    input.disabled = !c;
    updateCounter();
  }

  function renderName() {
    var n = state.S.gatewayName;
    $("self-name").textContent = n || "не задано";
    clearNameBtn.hidden = !state.S.nameAssigned;
    /* кнопки имени доступны только при живой связи (это команды на плату) */
    nameBtn.disabled = !state.S.connected;
    clearNameBtn.disabled = !state.S.connected;
    renderConnection();
  }

  function renderSelf() {
    $("self-id").textContent = state.S.selfId != null ? route.hex(state.S.selfId) : "—";
    $("self-session").textContent = state.S.selfSession != null
      ? "0x" + state.S.selfSession.toString(16).toUpperCase() : "—";
  }

  function setPing(v) {
    lastPing = v;
    if (pingEl) pingEl.textContent = v != null ? v + " мс" : "—";
  }

  function setMtu(v) {
    if (mtuEl) mtuEl.textContent = v != null ? v : "—";
  }

  function tickUptime() {
    if (!uptimeStart || !uptimeEl) return;
    var s = Math.floor((Date.now() - uptimeStart) / 1000);
    var mm = Math.floor(s / 60), ss = s % 60;
    uptimeEl.textContent = mm + ":" + (ss < 10 ? "0" : "") + ss;
  }

  /* ---- Ошибки человеческим языком ---- */
  var ERR_TEXT = {
    busy: "Узел занят другим подключением",
    text_too_long: "Сообщение слишком длинное",
    name_too_long: "Имя слишком длинное (не более 24 байт)",
    not_implemented: "Реальный BLE пока недоступен"
  };

  function showErr(code, msg) {
    var text = ERR_TEXT[code] || msg || code;
    hint.textContent = text;
    hint.style.color = "var(--err)";
    setTimeout(function () {
      if (hint.textContent === text) {
        hint.textContent = "";
        hint.style.color = "";
      }
    }, 4000);
  }

  function renderStats() {
    var s = state.S.stats;
    $("st-sent").textContent = s.sent;
    $("st-delivered").textContent = s.delivered;
    $("st-failed").textContent = s.failed;
  }

  return {
    init: init,
    renderConnection: renderConnection,
    renderName: renderName,
    renderSelf: renderSelf,
    renderStats: renderStats,
    updateCounter: updateCounter,
    setPing: setPing,
    setMtu: setMtu,
    tickUptime: tickUptime,
    showErr: showErr
  };
})();