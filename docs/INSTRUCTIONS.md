# INSTRUCTIONS — как запускать, проверять и прошивать

Практическая инструкция по текущему состоянию проекта. Для теории —
`README.md` и `docs/WORKFLOW.md`; для этапов — `docs/stages/README.md`.

> **Сейчас:** клиент (этапы 1–8) и прошивка написаны, тесты 29/29,
> плата прошита и работает (H1). **Следующий шаг — H2**: подключить
> клиента к BLE-шлюзу из Chrome.

---

## 0. Подготовка окружения (один раз на сессию)

`pio`, `git` и `gcc` **не в PATH** по умолчанию. Перед любой командой:

```powershell
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
$env:PYTHONIOENCODING = "utf-8"
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat"
```

Без `PYTHONIOENCODING=utf-8` PlatformIO падает с `UnicodeEncodeError`
на русской локали.

| Инструмент | Где лежит | Нужно? |
|---|---|---|
| PlatformIO | `C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe` | да (полный путь) |
| Python | в PATH | да (сервер клиента) |
| MinGW g++ | winget `BrechtSanders.WinLibs.POSIX.UCRT` | да (для `pio test`) |
| Git | в PATH | да |
| Node.js | — | **нет** |

---

## 1. Тесты логики (L1 + L2) — без железа

Проверяют `mesh_core`: пакеты, TTL, дедупликацию, петли, маршрут,
сценарий 1→2→3, отсутствие шторма.

```powershell
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat\firmware"
& "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe" test `
    -e native_packet -e native_relay -e native_nodes -e native_sim
```

**Ожидаемый результат:**

```text
native_packet  test_packet  PASSED
native_relay   test_relay   PASSED
native_nodes   test_nodes   PASSED
native_sim     test_sim     PASSED
29 test cases: 29 succeeded
```

**Куда смотреть:**
- каждый тест — `firmware/test/<имя>/<имя>.cpp`
- логика — `firmware/lib/mesh_core/`
- если упал — строка `test_xxx.cpp:NN` указывает на конкретный assert

**Ловушка:** окружения разделены (`native_packet`, `native_relay`, …),
потому что у каждого test-файла свой `main()`. Общее `-e native` даёт
`multiple definition of main`.

---

## 2. Сборка прошивки — без прошивания

```powershell
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat\firmware"
& "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe" run -e esp32c3
```

**Ожидаемый результат:** `SUCCESS`, примерно `RAM 15%`, `Flash 90%`.

Артефакты — `firmware/.pio/build/esp32c3/firmware.bin`.

---

## 3. Прошивка платы ⚠️ требует согласования

**Прошивка плат — отдельное согласование каждый раз**
(`ESP32_C3_Mesh_Chat_Project_Plan.md` §9.3). Не выполнять без разрешения.

```powershell
# Плата должна быть на COM5. Если другой порт — подставить свой.
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat\firmware"
& "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe" run -e esp32c3 -t upload --upload-port COM5
```

**Ожидаемый результат:** `Hash of data verified`, `SUCCESS`,
`Hard resetting via RTS pin...`.

**Если порт не тот:** список портов —

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
# или
& "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe" device list
```

На этой машине: **COM5** — плата (`USB VID:PID=303A:1001`, USB-JTAG
ESP32-C3). COM3/COM4 — Bluetooth-порты Windows, их не трогать.

---

## 4. Логи платы (Serial)

```powershell
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat\firmware"
& "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe" device monitor --port COM5 --baud 115200
```

Выход — `Ctrl+C`. **Монитор держит порт** — перед прошивкой его надо
закрыть.

**Ожидаемый лог при старте:**

```text
[boot] ESP32-C3 Mesh Chat
[node] id=0xE808 session=0xNNNN
[espnow] init ok
[BLE] gateway started as MeshGate-E808
```

Потом каждые 10 с узел шлёт `HELLO` (в лог ничего не пишет).

**Куда смотреть:**
- `setup()` — `firmware/src/main.cpp` (~строка 410)
- если логи **не видны** — в `platformio.ini` должны стоять флаги
  `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` (без них `Serial`
  идёт в UART0 GPIO20/21, куда ничего не подключено)
- `ESP-ROM:...` и `Guru Meditation` — вывод ROM/паники, видны всегда

**Если `Guru Meditation Error` и перезагрузка:**

| Ошибка | Причина | Что делать |
|---|---|---|
| `Instruction access fault`, `MEPC=0`, до `[boot]` | несовместимый NimBLE | нужен `NimBLE-Arduino@^2.3.0` (1.4.x падает на ESP-IDF 5.4) |
| Нет `[boot]`, но LED мигает | Serial не в USB | проверить флаги `ARDUINO_USB_MODE=1` |
| `Load access fault` | битая сборка | `Remove-Item .pio\build\esp32c3 -Recurse` и пересобрать |

---

## 5. Веб-клиент (H2 — следующий шаг)

### 5.1. Запуск сервера

Web Bluetooth требует **защищённого контекста** — `file://` не годится
(`BLE_API.md` §1). Нужен `http://localhost`.

```powershell
# Вариант 1: из папки web
& "C:\Users\Aleksandr\Desktop\Project_ESPchat\web\serve.cmd"

# Вариант 2: вручную
python -m http.server 5500 --directory "C:\Users\Aleksandr\Desktop\Project_ESPchat\web"
```

Сервер работает в отдельном окне — **не закрывать** во время теста.

### 5.2. Открыть клиент

В **Chrome/Edge** (не Firefox/Safari — нет Web Bluetooth):

- `http://localhost:5500/` — **заглушка** (`mock.js`): три узла, потери,
  маршрут. Работает **без платы**, для проверки интерфейса.
- `http://localhost:5500/?mock=0` — **реальный BLE** (`ble.js`).
  ⏳ Этап 9 не написан — сейчас `ble.js` — заглушка, выдаст ошибку
  `not_implemented`. **Это и есть содержимое этапа 9.**
- `http://localhost:5500/?demo=1` — демо-режим: лента наполнена примерами.
- `http://localhost:5500/?diag=1` — подсветка рамками (отладка адаптива).

### 5.3. Что проверяем на H2 (одна плата, BLE)

Согласно `docs/TEST_PLAN.md` §4, шаг 1:

1. На плате идёт реклама `MeshGate-E808` (лог `[BLE] gateway started`).
2. В Chrome → `?mock=0` → кнопка «Подключиться» (это жест пользователя —
   автоподключение невозможно, `BLE_API.md` §1).
3. В диалоге Web Bluetooth выбрать **`MeshGate-E808`**.
4. Обмен `ping`/`pong` (клиент шлёт каждые 5 с, см. `web/js/app.js`).
5. Замерить реальный **MTU** — в панели «Узел и подключение» (`[ПРОВЕРИТЬ]`
   в `BLE_API.md` §3.1: обычно 247, иногда 517).
6. Проверить, что **ESP-NOW не участвует** — других плат нет.

**Куда смотреть:**
- статус BLE — панель справа (`web/js/ui/status.js` → `renderConnection`)
- пинг/MTU — блок «Пинг / MTU / Время сессии» (`status.js` → `setPing/setMtu`)
- команды клиента → шлюза — `web/js/app.js` (`sendTransport`, `transport.on`)
- приём на плате — `firmware/src/ble_gateway.cpp` (`feedBytes`,
  `processCompleteFrame`), отправка — `ble_gateway_send_json`
- **но**: `transport/ble.js` сейчас заглушка. H2 по-настоящему начнётся,
  когда напишем этап 9 (реальный Web Bluetooth в `ble.js`)

**Порядок H2 с учётом этого:**
1. Убедиться, что плата рекламит (`MeshGate-E808` видна в `bluetooth.low
   energy` сканере или в Chrome).
2. Написать этап 9 — реальный `ble.js` (Web Bluetooth: `requestDevice`,
   GATT-подключение, кадрирование `protocol.js`, `ping/pong`).
3. Подключиться, замерить MTU, проверить `ping/pong`.
4. Закоммитить этап 9 + создать `docs/stages/client-09-ble.md`.

---

## 6. Быстрая сводка команд

```powershell
# подготовка
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
$env:PYTHONIOENCODING = "utf-8"
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat"
$pio = "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe"

# тесты (всегда, перед коммитом кода)
Set-Location firmware
& $pio test -e native_packet -e native_relay -e native_nodes -e native_sim

# сборка (без прошивания)
& $pio run -e esp32c3

# прошивка ⚠️ только с согласования
& $pio run -e esp32c3 -t upload --upload-port COM5

# логи
& $pio device monitor --port COM5 --baud 115200

# клиент
python -m http.server 5500 --directory ..\web
# → http://localhost:5500/

# git
Set-Location ..
git add -A
git commit -q -F ".git\COMMIT_MSG_TMP.txt"
Remove-Item ".git\COMMIT_MSG_TMP.txt"
git push origin main
```

---

## 7. Что дальше (карта)

| # | Что | Зависит от | Статус |
|---|---|---|---|
| 1 | **H2**: реальный `ble.js` + подключение к `MeshGate-E808` | плата 1 | ⏳ **сейчас** |
| 2 | ESP-NOW вживую: прямая передача, RSSI | плата 2 | ⬜ |
| 3 | BLE + ESP-NOW одновременно (самый рискованный) | плата 2 | ⬜ |
| 4 | H3: демо 1→2→3, показ маршрута | плата 3 | ⬜ |
| 5 | Этап 9 клиента закрыт | п.1 | ⬜ |

**Перед H2:** нужна ли вторая плата? **Нет** — H2 делается на одной.

**Ловушка H3** (`TEST_PLAN` §4): если узел 3 слышит узел 1 напрямую,
маршрут будет прямым и ретрансляция не покажется. Заложить в сценарий
заранее: разнести платы физически или запретить прямой канал.

---

## 8. Частые проблемы

| Симптом | Причина | Решение |
|---|---|---|
| `pio: not recognized` | PATH не восстановлен | §0 |
| `UnicodeEncodeError` | нет `PYTHONIOENCODING` | §0 |
| `'gcc' is not recognized` | нет MinGW в PATH | §0 (winget уже ставил) |
| `Not a PlatformIO project` | не в `firmware/` | `Set-Location firmware` |
| `multiple definition of main` | все тесты в одном env | использовать 4 env по отдельности |
| Логи платы пустые | Serial в UART0 | `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` |
| `web bluetooth` не виден | `file://` или не Chrome | `http://localhost` + Chrome |
| Пуш падает `Win32 error 5` | нет полного доступа DSH | `WORKFLOW.md` §3.3 |
| Плата не прошивается | монитор держит порт | закрыть `device monitor` |
