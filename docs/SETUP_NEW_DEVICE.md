# Перенос проекта на новое устройство: прошивка плат и запуск клиента

> Самодостаточная инструкция: как поднять проект на новом ПК — перенести
> код, собрать и прошить платы, запустить веб-клиент и подключиться к
> шлюзу. Здесь только перенос и запуск; для разработки см.
> `../PROJECT_BRIEF.md` и `INSTRUCTIONS.md`.
>
> Команды даны для Windows PowerShell. На macOS/Linux всё то же самое:
> `pip3 install platformio`, `python3 -m http.server 5500 -d web`,
> порт платы — `/dev/ttyUSB0` или `/dev/ttyACM0`.

## 0. Что понадобится

| Что | Зачем |
|---|---|
| Git | забрать код из репозитория |
| **Chrome или Edge** (Chromium) | Web Bluetooth работает только в них |
| Bluetooth-адаптер на компьютере | связь с платой |
| **Python 3.8+** (любой настоящий) | PlatformIO + сервер для клиента |
| MinGW g++ — только для нативных тестов mesh-ядра | `pio test` |
| 1–3 платы **ESP32-C3** (esp32-c3-devkitm-1) | узлы mesh |

Драйверы для платы обычно не нужны: ESP32-C3 видна как обычный
COM-порт (USB-CDC) на Windows 10/11.

## 1. Перенести код

```powershell
git clone https://github.com/Alex171012/ESP32-Mesh-Chat.git
Set-Location ESP32-Mesh-Chat
```

Альтернатива — скачать zip с GitHub, но обновления тогда придётся
переносить вручную.

## 2. Установить PlatformIO (один раз)

```powershell
pip install -U platformio
```

После установки CLI живёт в `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`
(в PATH может не появиться сразу). Проверка:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" --version
```

Каждое новое окно PowerShell начинать с подготовки окружения:

```powershell
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
$env:PYTHONIOENCODING = "utf-8"   # иначе UnicodeEncodeError на русской локали
```

Зависимости прошивки (NimBLE и др.) подтянутся автоматически при
первой сборке. Для нативных тестов нужен g++ в PATH:
`winget install BrechtSanders.WinLibs.POSIX.UCRT`.

## 3. Собрать и прогнать тесты (без железа)

```powershell
Set-Location firmware
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio test -e native_packet -e native_relay -e native_nodes -e native_sim
& $pio run -e esp32c3
```

Ожидаемо: `29 test cases: 29 succeeded`; сборка `SUCCESS`
(Flash около 90%). Артефакт — `.pio/build/esp32c3/firmware.bin`.

Прошивка **одна на все платы**: узлы и имена шлюза (`MeshGate-XXXX`)
платы получают сами из своих MAC-адресов, никакой ручной привязки нет.

## 4. Прошить плату ⚠️ только с разрешения пользователя

Правило проекта: каждая прошивка — отдельное согласование.

```powershell
# Список портов (или pio device list):
[System.IO.Ports.SerialPort]::GetPortNames()

Set-Location firmware
& $pio run -e esp32c3 -t upload --upload-port COMx   # подставить свой порт
```

Ожидаемо: `Hash of data verified`, `SUCCESS`,
`Hard resetting via RTS pin...`.

Проверка по Serial (Ctrl+C — выход):

```powershell
& $pio device monitor --port COMx --baud 115200
```

Успешный запуск платы:

```text
[boot] ESP32-C3 Mesh Chat
[node] id=0xXXXX session=0xYYYY
[espnow] init ok
[BLE] gateway started as MeshGate-XXXX
[BLE] advertising: OK
```

Ловушки:

- **Любая программа, открывшая COM-порт, может перезагрузить плату**
  (DTR/RTS у USB-CDC). Перед прошивкой закрыть монитор и другие
  терминалы порта.
- **Нельзя держать кнопку BOOT (GPIO9) при включении платы** — это
  strapping-пин, не аварийный сброс.
- Свежесобранные unsigned-бинари могут блокироваться Windows Smart
  App Control (WinError 4551) — тогда нативные тесты на этом ПК не
  запускаются; сборка `esp32c3` не затронута.

## 5. Запустить веб-клиент

Клиент без сборки и зависимостей — нужен любой Python:

```powershell
Set-Location <путь к репозиторию>
python -m http.server 5500 --directory web
```

(В проекте есть готовый `web\serve.cmd`; на исходном ПК он использует
встроенный Python — на новом ПК достаточно системного.)

Открыть в Chrome:

- `http://localhost:5500/?mock=0` — **с реальной платой**;
- `http://localhost:5500/?mock=1` — заглушка, проверка UI без железа.

## 6. Первое подключение

1. Кнопка «Подключить» → в диалоге Chrome выбрать `MeshGate-XXXX`.
2. Подключение, MTU до 256, автоматический ping — RTT порядка
   100–200 мс, показывается сразу.
3. **Первый запуск:** приложение спросит имя узла (до 24 байт,
   русские буквы — по 2 байта). Ограничение текущей версии: имя живёт
   до перезагрузки платы, сохранение в NVS ещё в планах.
4. Отправить сообщение полем ввода. Статусы: `accepted` → `sent`
   → `failed` (~5 с), **если плата одна** — подтверждению доставки
   некому прийти. С двумя и более платами — `delivered` с маршрутом
   и числом прыжков.

## 7. Ограничения Web Bluetooth

- Только браузеры на Chromium (Chrome, Edge). Firefox/Safari — нет.
- Нужен secure context: `http://localhost` подходит, `file://` — нет.
- Открыть клиент **с телефона по `http://<IP-адрес>` не выйдет**:
  BLE заблокируют как небезопасный контекст. Либо запускать на самом
  устройстве через localhost, либо поднимать HTTPS.
- Chrome запоминает выбранное устройство. Если плата перепрошита и
  начала рекламироваться под другим именем/ID — повторно выбрать её
  через «Подключить».

## 8. Частые проблемы

| Симптом | Причина | Решение |
|---|---|---|
| `pio` не найден | не восстановлен PATH | §2, блок `$env:Path` |
| `UnicodeEncodeError` | нет `PYTHONIOENCODING` | `$env:PYTHONIOENCODING="utf-8"` |
| `multiple definition of main` | тесты в одном env | запускать 4 env по отдельности (§3) |
| Сборка `Not a PlatformIO project` | не в `firmware/` | `Set-Location firmware` |
| Лог платы пуст | (уже настроено) CDC-флаги в `platformio.ini` | не убирать `-DARDUINO_USB_*` |
| Порт занят / плата перезагружается | монитор держит COM; DTR/RTS | закрыть монитор — это норма для USB-CDC |
| Нет диалога выбора устройства | не Chrome/Edge или `file://` | `http://localhost` + Chromium |
| Прошивка не начинается | порт занят другим процессом | закрыть всё, что держит COMx |
