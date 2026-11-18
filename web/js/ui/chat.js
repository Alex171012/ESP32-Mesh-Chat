/* chat.js — лента сообщений: рендер, маршрут, четыре статуса */

var chat = (function () {
  "use strict";

  var feed, emptyEl;

  var STATUS_TEXT = {
    accepted: "принято шлюзом",
    sent: "в пути",
    delivered: "доставлено",
    failed: "не доставлено",
    unsent: "не отправлено"
  };

  function init() {
    feed = document.getElementById("feed");
    emptyEl = document.getElementById("feed-empty");
    updateEmpty();
  }

  function updateEmpty() {
    /* через style, т.к. класс .feed-empty имеет display и перекрывает [hidden] */
    emptyEl.style.display = state.S.messages.length ? "none" : "";
  }

  /* Время: метка ставится на клиенте (MESH_PROTOCOL §6.3) */
  function timeLabel(ts) {
    var d = new Date(ts || Date.now());
    function p(n) { return (n < 10 ? "0" : "") + n; }
    return p(d.getHours()) + ":" + p(d.getMinutes());
  }

  function authorOf(m) {
    if (m.out) return "Я";
    return m.src_name || ("Узел " + route.hex(m.src));
  }

  /* ---- Подвал: маршрут + статус ---- */
  function renderFoot(m) {
    var foot = document.createElement("div");
    foot.className = "msg-foot";

    /* маршрут: path не содержит получателя, его добавляет route.build */
    var ids = (m.path && m.path.length) ? m.path.slice() : null;
    if (ids) {
      var r = document.createElement("span");
      r.className = "route";
      var dest = m.dst != null ? m.dst : null;
      var frag = route.build(ids, dest);
      if (frag) r.appendChild(frag);
      foot.appendChild(r);
    } else if (m.out && m.dst === 0) {
      var rb = document.createElement("span");
      rb.className = "route route-none";
      rb.textContent = "broadcast";
      foot.appendChild(rb);
    }

    /* статус */
    if (m.status) {
      var s = document.createElement("span");
      s.className = "status " + m.status;
      s.textContent = STATUS_TEXT[m.status] || m.status;
      foot.appendChild(s);
    }

    /* кнопка «Повторить» */
    if (m.status === "unsent" || (m.status === "failed" && m.out)) {
      var b = document.createElement("button");
      b.className = "retry-btn";
      b.textContent = "Повторить";
      b.addEventListener("click", function () {
        if (typeof m.onRetry === "function") m.onRetry();
      });
      foot.appendChild(b);
    }

    return foot;
  }

  /* ---- Одно сообщение ---- */
  function render(m) {
    var node = document.createElement("div");
    node.className = "msg " + (m.out ? "out" : "in");
    node.dataset.id = String(m.id);
    if (m.status === "failed" || m.status === "unsent") node.classList.add(m.status);

    var head = document.createElement("div");
    head.className = "msg-head";

    var author = document.createElement("span");
    author.className = "msg-author";
    author.textContent = authorOf(m);

    var time = document.createElement("span");
    time.className = "msg-time";
    time.textContent = timeLabel(m.localTs);

    head.appendChild(author);
    head.appendChild(time);

    if (m.dst && m.dst !== 0 && m.out) {
      var d = document.createElement("span");
      d.className = "msg-dst";
      d.textContent = "→ " + (m.dstName || route.hex(m.dst));
      head.appendChild(d);
    }

    var bubble = document.createElement("div");
    bubble.className = "bubble";
    bubble.textContent = m.text;

    node.appendChild(head);
    node.appendChild(bubble);
    node.appendChild(renderFoot(m));
    return node;
  }

  function scrollDown() {
    feed.scrollTop = feed.scrollHeight;
  }

  function add(m) {
    emptyEl.style.display = "none";
    feed.appendChild(render(m));
    scrollDown();
  }

  /* Обновление без перерисовки всей ленты: только подвал сообщения */
  function update(m) {
    var sel = '.msg[data-id="' + String(m.id).replace(/"/g, "") + '"]';
    var node = feed.querySelector(sel);
    if (!node) { add(m); return; }
    var cls = "msg " + (m.out ? "out" : "in");
    if (m.status === "failed" || m.status === "unsent") cls += " " + m.status;
    node.className = cls;
    var old = node.querySelector(".msg-foot");
    if (old) node.replaceChild(renderFoot(m), old);
    else node.appendChild(renderFoot(m));
  }

  return { init: init, add: add, update: update };
})();