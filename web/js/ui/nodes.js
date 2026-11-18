/* nodes.js — панель узлов: соседи и mesh-узлы раздельно */

var nodesUI = (function () {
  "use strict";

  var nearList, meshList, dstSelect;
  var selected = 0;

  function init() {
    nearList = document.getElementById("nodes-near");
    meshList = document.getElementById("nodes-mesh");
    dstSelect = document.getElementById("dst-select");
    dstSelect.addEventListener("change", function () {
      selected = Number(dstSelect.value);
      state.setDst(selected);
      renderAll(state.S.nodes);
    });
    renderTargets();
  }

  function hex(id) { return route.hex(id); }

  function nameOf(n) { return n.name || ("Узел " + hex(n.id)); }

  /* ---- Переключатель получателя (BLE_API §7.1) ---- */
  function renderTargets() {
    var nodes = state.S.nodes;
    dstSelect.innerHTML = "";

    var optAll = document.createElement("option");
    optAll.value = "0";
    optAll.textContent = "Всем";
    dstSelect.appendChild(optAll);

    for (var i = 0; i < nodes.length; i++) {
      var n = nodes[i];
      var o = document.createElement("option");
      o.value = String(n.id);
      o.textContent = nameOf(n) + (n.online ? "" : " (offline)");
      dstSelect.appendChild(o);
    }

    dstSelect.value = String(selected);
    if (Number(dstSelect.value) !== selected) {
      selected = 0;
      dstSelect.value = "0";
      state.setDst(0);
    }
  }

  function renderHeader() {
    var nameEl = document.getElementById("chat-peer-name");
    var subEl = document.getElementById("chat-peer-sub");
    if (selected === 0) {
      nameEl.textContent = "Всем узлам";
      subEl.textContent = "общий канал · dst 0x0000";
    } else {
      var n = null;
      for (var i = 0; i < state.S.nodes.length; i++) {
        if (state.S.nodes[i].id === selected) { n = state.S.nodes[i]; break; }
      }
      nameEl.textContent = n ? nameOf(n) : hex(selected);
      subEl.textContent = "личное · " + hex(selected) +
        (n && !n.online ? " · offline" : "");
    }
  }

  /* ---- Список узлов ---- */
  function nodeItem(n) {
    var li = document.createElement("li");
    var btn = document.createElement("button");
    btn.className = "node " + (n.online ? "online" : "offline");
    if (n.id === selected) btn.classList.add("selected");

    var avatar = document.createElement("span");
    avatar.className = "node-avatar";
    avatar.textContent = (n.name || "?").slice(0, 1).toUpperCase();
    var pres = document.createElement("span");
    pres.className = "node-presence";
    avatar.appendChild(pres);

    var body = document.createElement("div");
    body.className = "node-body";

    var nm = document.createElement("div");
    nm.className = "node-name";
    nm.textContent = nameOf(n);

    var meta = document.createElement("div");
    meta.className = "node-meta";
    meta.textContent = hex(n.id) + (n.online ? "" : " · offline");

    body.appendChild(nm);
    body.appendChild(meta);

    btn.appendChild(avatar);
    btn.appendChild(body);

    if (n.rssi != null) {
      var r = document.createElement("span");
      r.className = "node-rssi";
      r.textContent = n.rssi + " dBm";
      btn.appendChild(r);
    }

    btn.addEventListener("click", function () {
      selected = n.id;
      state.setDst(n.id);
      renderAll(state.S.nodes);
      var col = document.querySelector(".col-nodes");
      if (col) col.classList.remove("open");
    });

    li.appendChild(btn);
    return li;
  }

  function fill(listEl, items) {
    listEl.innerHTML = "";
    if (!items.length) {
      var li = document.createElement("li");
      li.className = "node-empty";
      li.textContent = "нет";
      listEl.appendChild(li);
      return;
    }
    for (var i = 0; i < items.length; i++) listEl.appendChild(nodeItem(items[i]));
  }

  function renderAll(list) {
    var near = [], mesh = [];
    for (var i = 0; i < list.length; i++) {
      if (list[i].neighbor) near.push(list[i]);
      else mesh.push(list[i]);
    }
    fill(nearList, near);
    fill(meshList, mesh);
    renderTargets();
    renderHeader();
    renderStats(list);
  }

  function renderStats(list) {
    var online = 0, neighbors = 0;
    for (var i = 0; i < list.length; i++) {
      if (list[i].online) online++;
      if (list[i].neighbor) neighbors++;
    }
    var set = function (id, v) {
      var e = document.getElementById(id);
      if (e) e.textContent = v;
    };
    set("net-count", list.length);
    set("net-neighbors", neighbors);
    set("net-online", online);
  }

  return { init: init, renderAll: renderAll, renderHeader: renderHeader };
})();