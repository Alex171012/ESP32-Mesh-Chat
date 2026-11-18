/* protocol.js — кадрирование SOF|len|payload|crc16 и JSON (BLE_API §3-4)
 * Чистые функции: без DOM, без транспорта.
 * Реализация ES5-совместимая (без arrow/const/template — для проверки
 * внешним парсером и для старых движков).
 */

var protocol = (function () {
  "use strict";

  /* ---- CRC-16/CCITT (poly 0x1021, init 0xFFFF) ---- */
  function crc16(bytes) {
    var crc = 0xffff;
    for (var i = 0; i < bytes.length; i++) {
      crc ^= bytes[i] << 8;
      for (var b = 0; b < 8; b++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
        crc &= 0xffff;
      }
    }
    return crc;
  }

  function utf8Encode(str) {
    return new TextEncoder().encode(str);
  }

  function utf8Decode(bytes) {
    return new TextDecoder("utf-8", { fatal: false }).decode(bytes);
  }

  /* ---- encode: объект → кадр [SOF | len(LE) | payload | crc16(LE)] ---- */
  function encode(obj) {
    var payload = utf8Encode(JSON.stringify(obj));
    var frame = new Uint8Array(1 + 2 + payload.length + 2);
    frame[0] = CONFIG.SOF;
    frame[1] = payload.length & 0xff;             /* len little-endian */
    frame[2] = (payload.length >> 8) & 0xff;
    frame.set(payload, 3);
    var crc = crc16(payload);
    frame[3 + payload.length] = crc & 0xff;
    frame[4 + payload.length] = (crc >> 8) & 0xff;
    return frame;
  }

  /* ---- decode: кадр → объект; null при ошибке ---- */
  function decode(frame) {
    if (frame.length < 5 || frame[0] !== CONFIG.SOF) return null;
    var len = frame[1] | (frame[2] << 8);
    if (frame.length !== len + 5) return null;
    var payload = frame.subarray(3, 3 + len);
    var got = frame[3 + len] | (frame[4 + len] << 8);
    if (got !== crc16(payload)) return null;
    try {
      return JSON.parse(utf8Decode(payload));
    } catch (e) {
      return null;
    }
  }

  /* ---- Буфер приёма: склейка частей, поиск SOF, восстановление после мусора ---- */
  function createFeeder(onError) {
    var buf = new Uint8Array(0);

    function push(chunk) {
      var merged = new Uint8Array(buf.length + chunk.length);
      merged.set(buf, 0);
      merged.set(chunk, buf.length);
      buf = merged;
    }

    return function feed(chunk) {
      if (chunk && chunk.length) push(chunk);
      var frames = [];

      for (;;) {
        /* 1. ищем SOF */
        var sof = indexOfSof(buf);
        if (sof < 0) { buf = new Uint8Array(0); break; }
        if (sof > 0) {
          if (onError) onError("мусор до SOF: " + sof + " байт отброшено");
          buf = buf.subarray(sof);
        }
        /* 2. хватает ли байт на заголовок */
        if (buf.length < 3) break;
        var len = buf[1] | (buf[2] << 8);
        var total = len + 5;
        if (total > 4096) { /* защита от битого len */
          if (onError) onError("подозрительная длина кадра: " + len);
          buf = buf.subarray(1);
          continue;
        }
        /* 3. полный ли кадр */
        if (buf.length < total) break;
        var frame = buf.subarray(0, total);
        var obj = decode(frame);
        if (obj === null) {
          if (onError) onError("ошибка SOF/CRC в кадре длиной " + total);
          buf = buf.subarray(1);      /* пропускаем байт и ищем следующий SOF */
          continue;
        }
        frames.push(obj);
        buf = buf.subarray(total);    /* 4. остаток — следующий кадр */
      }
      return frames;
    };
  }

  function indexOfSof(arr) {
    for (var i = 0; i < arr.length; i++) {
      if (arr[i] === CONFIG.SOF) return i;
    }
    return -1;
  }

  return { crc16: crc16, encode: encode, decode: decode, createFeeder: createFeeder };
})();