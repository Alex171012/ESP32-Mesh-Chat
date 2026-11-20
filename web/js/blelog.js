/* blelog.js — видимая отладочная панель BLE (этап 9, отладка H2).
 *
 * Проблема: ошибки подключения Web Bluetooth видны только в F12-консоли,
 * пользователь их не видит. Эта панель дублирует все BLE-события прямо
 * на странице: каждый шаг connect, notify, disconnect, err.
 *
 * API: blelog.log(tag, msg), blelog.warn(tag, msg), blelog.error(tag, msg),
 *      blelog.clear(), blelog.toggle()
 * ES5, без зависимостей. Подключать ДО ble.js.
 */

var blelog = (function () {
  "use strict";

  var MAX_LINES = 200;
  var box = null;        /* контейнер панели */
  var listEl = null;     /* поток строк */
  var collapsed = false;
  var count = 0;

  function ts() {
    var d = new Date();
    function p(n, w) { n = String(n); while (n.length < (w || 2)) n = "0" + n; return n; }
    return p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds()) +
           "." + p(d.getMilliseconds(), 3);
  }

  function esc(s) {
    return String(s)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function append(level, tag, msg) {
    var line = "[" + ts() + "] " + (tag ? tag + ": " : "") + msg;
    /* Зеркало в консоль — F12 остаётся источником полной информации */
    if (level === "error") console.error("[ble]", line);
    else if (level === "warn") console.warn("[ble]", line);
    else console.log("[ble]", line);

    if (!listEl) return;
    var div = document.createElement("div");
    div.className = "blelog-line blelog-" + level;
    div.textContent = line;
    listEl.appendChild(div);
    count++;
    while (count > MAX_LINES && listEl.firstChild) {
      listEl.removeChild(listEl.firstChild);
      count--;
    }
    listEl.scrollTop = listEl.scrollHeight;
  }

  function ensurePanel() {
    if (box || typeof document === "undefined") return;
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", ensurePanel);
      return;
    }

    box = document.createElement("div");
    box.id = "blelog-panel";
    box.style.cssText =
      "position:fixed;bottom:8px;right:8px;width:420px;max-width:95vw;" +
      "max-height:45vh;background:#111;color:#cfe;font:11px/1.45 monospace;" +
      "border:1px solid #356;border-radius:6px;z-index:9999;overflow:hidden;" +
      "box-shadow:0 2px 10px rgba(0,0,0,.5);display:flex;flex-direction:column;";

    var head = document.createElement("div");
    head.style.cssText =
      "padding:4px 8px;background:#1b2735;cursor:pointer;user-select:none;" +
      "display:flex;justify-content:space-between;flex-shrink:0;";
    head.innerHTML =
      '<span>BLE отладка</span>' +
      '<span style="opacity:.7">— / очистить: ✕</span>';

    var btnClear = document.createElement("span");
    btnClear.textContent = " ✕";
    btnClear.style.cssText = "cursor:pointer;margin-left:8px;opacity:.8;";
    btnClear.title = "Очистить лог";

    listEl = document.createElement("div");
    listEl.style.cssText = "overflow-y:auto;padding:4px 8px;flex:1;";

    head.addEventListener("click", function () {
      collapsed = !collapsed;
      listEl.style.display = collapsed ? "none" : "block";
    });
    btnClear.addEventListener("click", function (ev) {
      ev.stopPropagation();
      listEl.innerHTML = "";
      count = 0;
    });

    head.appendChild(btnClear);
    box.appendChild(head);
    box.appendChild(listEl);
    document.body.appendChild(box);
  }

  function log(tag, msg)   { append("log", tag, msg); ensurePanel(); }
  function warn(tag, msg)  { append("warn", tag, msg); ensurePanel(); }
  function error(tag, msg) { append("error", tag, msg); ensurePanel(); }
  function clear()         { if (listEl) { listEl.innerHTML = ""; count = 0; } }
  function toggle() {
    ensurePanel();
    if (box) box.style.display = box.style.display === "none" ? "flex" : "none";
  }

  /* Публичный объект */
  return {
    log: log,
    warn: warn,
    error: error,
    clear: clear,
    toggle: toggle
  };
})();
