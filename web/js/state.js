/* state.js — состояние приложения и подписки */

var state = (function () {
  "use strict";

  var S = {
    connected: false,
    connecting: false,
    gateway: null,          /* node_id шлюза */
    gatewayName: null,      /* имя шлюза (null = не задано) */
    nameAssigned: false,
    selfId: null,           /* node_id своего узла */
    selfSession: null,

    nodes: [],              /* [{id, name, neighbor, online, rssi, last_seen_ms}] */
    dst: 0,                 /* 0 = всем */

    messages: [],           /* лента */
    stats: { sent: 0, delivered: 0, failed: 0 },

    ble: { ping: null, mtu: null, since: null }
  };

  var subs = {};            /* event -> [cb] */

  function on(event, cb) {
    if (!subs[event]) subs[event] = [];
    subs[event].push(cb);
  }

  function emit(event, payload) {
    var list = subs[event] || [];
    for (var i = 0; i < list.length; i++) {
      try { list[i](payload); }
      catch (e) { console.error("[" + event + "]", e); }
    }
  }

  /* ---- Обновления состояния + рассылка событий ---- */

  function setConnected(v, gateway) {
    S.connected = v;
    S.connecting = false;
    if (v && gateway != null) S.gateway = gateway;
    if (!v) S.ble.since = null;
    emit("ble", { connected: v, gateway: S.gateway });
  }

  function setConnecting(v) {
    S.connecting = v;
    emit("ble", { connected: false, connecting: v, gateway: S.gateway });
  }

  function setNodes(list) {
    S.nodes = list || [];
    emit("nodes", S.nodes);
  }

  function setSelf(id, session) {
    S.selfId = id;
    S.selfSession = session;
  }

  function setName(name, assigned) {
    S.gatewayName = name;
    S.nameAssigned = assigned;
    emit("name", { name: name, assigned: assigned });
  }

  function setDst(id) {
    S.dst = id;
    emit("dst", id);
  }

  function addMessage(msg) {
    S.messages.push(msg);
    emit("msg:add", msg);
  }

  function updateMessage(id, patch) {
    var m = null;
    for (var i = 0; i < S.messages.length; i++) {
      if (S.messages[i].id === id) { m = S.messages[i]; break; }
    }
    if (!m) return null;
    Object.keys(patch).forEach(function (k) { m[k] = patch[k]; });
    emit("msg:update", m);
    return m;
  }

  function setStats(patch) {
    Object.keys(patch).forEach(function (k) { S.stats[k] = patch[k]; });
    emit("stats", S.stats);
  }

  return {
    S: S,
    on: on, emit: emit,
    setConnected: setConnected, setConnecting: setConnecting,
    setNodes: setNodes, setSelf: setSelf, setName: setName, setDst: setDst,
    addMessage: addMessage, updateMessage: updateMessage, setStats: setStats
  };
})();