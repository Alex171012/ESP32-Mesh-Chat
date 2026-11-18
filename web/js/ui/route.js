/* route.js — отображение маршрута (главный элемент демонстрации) */

var route = (function () {
  "use strict";

  function hex(id) {
    var v = id >>> 0;
    var s = v.toString(16).toUpperCase();
    while (s.length < 4) s = "0" + s;
    return "0x" + s;
  }

  function el(tag, cls, text) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }

  /* Маршрут массивом node_id → фрагмент с хвостом-получателем */
  function build(ids, destId) {
    if (!ids || !ids.length) return null;
    var frag = document.createDocumentFragment();
    for (var i = 0; i < ids.length; i++) {
      if (i) frag.appendChild(el("span", "route-arrow", "→"));
      frag.appendChild(el("span", "route-node", hex(ids[i])));
    }
    if (destId != null && ids[ids.length - 1] !== destId) {
      frag.appendChild(el("span", "route-arrow", "→"));
      frag.appendChild(el("span", "route-node dest", hex(destId)));
    }
    return frag;
  }

  /* Строка для простых мест (списки, заголовки) */
  function text(ids, destId) {
    if (!ids || !ids.length) return "";
    var parts = [];
    for (var i = 0; i < ids.length; i++) parts.push(hex(ids[i]));
    if (destId != null && ids[ids.length - 1] !== destId) parts.push(hex(destId));
    return parts.join(" → ");
  }

  return { hex: hex, build: build, text: text };
})();