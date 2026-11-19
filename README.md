# ESP32-C3 Mesh Chat

Текстовая чат-сеть на трёх платах **ESP32-C3 SuperMini** без интернета,
Wi-Fi-роутера и сотовой связи. Связь между платами — **ESP-NOW**, связь
телефона или ноутбука с платой — **Bluetooth Low Energy**. Все платы
прошиты одной и той же прошивкой; та, к которой подключён клиент, временно
работает как шлюз.

Клиент — веб-приложение, открываемое в Chromium (Windows/Linux/macOS/Android).
Это статические HTML/CSS/JS: сборка не нужна, но для реального BLE
требуется локальный сервер (`http://localhost`), потому что `file://`
не является защищённым контекстом.

> **Статус:** реализация начата. Веб-клиент (этапы 1–8) и каркас
> прошивки написаны, тесты L1/L2 зелёные (29/29). Плата прошита и работает
> (H1 завершён). Числовые параметры протокола помечены `[ПРОВЕРИТЬ]` и
> требуют проверки на реальных платах.
> **Следующий шаг:** H2 — подключение клиента к BLE-шлюзу из Chrome.

---

## Документы

| Документ | Содержание |
|---|---|
| [PROJECT_PLAN](ESP32_C3_Mesh_Chat_Project_Plan.md) | Основной план: цели, этапы, критерии готовности, риски |
| [MESH_PROTOCOL](protocol/MESH_PROTOCOL.md) | Формат пакетов, ID, TTL, дедупликация, маршрут, ACK |
| [BLE_API](protocol/BLE_API.md) | GATT-профиль, кадрирование, формат сообщений, правила поведения |
| [ARCHITECTURE](docs/ARCHITECTURE.md) | Схема, границы модулей, потоки данных |
| [TEST_PLAN](docs/TEST_PLAN.md) | Три уровня проверок, критерии готовности |
| [WEB_CLIENT_PLAN](docs/WEB_CLIENT_PLAN.md) | План клиента: структура, этапы, ограничения платформ |
| [WORKFLOW](docs/WORKFLOW.md) | Репозиторий, правила коммитов, начало работы в новом чате |

Читать в этом порядке. `WORKFLOW.md` — первым, если начинается новый чат.
`docs/INSTRUCTIONS.md` — если нужно запустить и проверить.

---

## Структура репозитория

```text
Project_ESPchat/
├── firmware/
│   ├── platformio.ini        ← окружения: native (тесты) + esp32c3 (прошивка)
│   ├── sdkconfig.defaults    ← фиксация NimBLE (без Bluedroid)
│   ├── lib/mesh_core/        ← логика БЕЗ Arduino и ESP-IDF (packet, relay, …)
│   ├── src/                  ← main, ble_gateway, espnow_link, config
│   └── test/                 ← test_packet, test_relay, test_nodes, test_sim
├── web/
│   ├── index.html
│   ├── css/                  ← base, layout, components
│   ├── js/
│   │   ├── ui/               ← chat, nodes, route, status
│   │   └── transport/        ← ble.js (заглушка) и mock.js
│   └── serve.cmd             ← локальный сервер (см. docs/INSTRUCTIONS.md §5.1)
├── protocol/                 ← BLE_API, MESH_PROTOCOL
├── docs/                     ← WORKFLOW, TEST_PLAN, ARCHITECTURE, WEB_CLIENT_PLAN
│   └── stages/               ← client-01…08 (самодостаточные задания)
├── ESP32_C3_Mesh_Chat_Project_Plan.md
└── README.md
```

---

## Репозиторий

Проект ведётся в публичном репозитории
[Alex171012/ESP32-Mesh-Chat](https://github.com/Alex171012/ESP32-Mesh-Chat).
Работа идёт в ветке `main`.

**Правило:** коммит и пуш после каждого логического блока — законченного
изменения, а не каждого сохранения файла. Пуш сразу после коммита
обязателен: непушенные коммиты легко потерять.

**Порядок коммита** (PowerShell не поддерживает heredoc, поэтому
сообщение пишется в файл):

```powershell
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat"
git add -A
git commit -q -F ".git\COMMIT_MSG_TMP.txt"
Remove-Item ".git\COMMIT_MSG_TMP.txt"
git push origin main
```

**Вывод git идёт в stderr.** PowerShell показывает его как
`NativeCommandError` — это не ошибка. Признак успеха — `exit code: 0`.

**Перед коммитом** проверять, что не попали секреты и реальные
MAC-адреса: репозиторий публичный, а `node_id` выводится из MAC.
Подробно — в [WORKFLOW](docs/WORKFLOW.md) §3.2.

Инструменты: Git 2.56.0.4 (`C:\Program Files\Git\cmd`), GitHub CLI 2.104.0
(`C:\Program Files\GitHub CLI\gh.exe`), вход через `gh auth login`.

---

## Окружение

Проверено на текущей машине (Windows 11):

- PlatformIO Core **6.2.0**
- Платформа Espressif 32 **8.0.0**, компилятор riscv32-esp **14.2.0**
- Arduino-ядро 3.3.12
- Сборка тестового проекта для ESP32-C3 — **успешно**

### Важные особенности запуска

**`pio` не прописан в PATH.** Вызывать по полному пути:

```powershell
$env:PYTHONIOENCODING = "utf-8"
& "C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe" run
```

Переменная `PYTHONIOENCODING=utf-8` **обязательна**: на русской локали без
неё PlatformIO падает с `UnicodeEncodeError`.

**Тулчейн лежит вне проекта** — в `C:\Users\Aleksandr\.platformio`
(≈4,2 ГБ). Агенту нужен доступ и к этой папке, иначе сборка невозможна.

**Файлы конфигурации писать без BOM.** PowerShell 5.1 в режиме
`Set-Content -Encoding UTF8` добавляет BOM, из-за чего PlatformIO выдаёт
`InvalidProjectConfError`. Использовать
`[System.IO.File]::WriteAllText()` с `UTF8Encoding($false)`.

| Что | Состояние |
|---|---|
| Git | 2.56.0.4, в PATH |
| GitHub CLI | 2.104.0, вход выполнен |
| PlatformIO Core | 6.2.0, **не в PATH** |
| VS Code | расширения: C/C++, PlatformIO IDE |
| Python | системного **нет**; рабочий — bundled в DSH-runtime (для сервера клиента) |
| Node.js | системного **нет**; bundled в DSH-runtime — серверу не нужен |
| USB-serial | **обнаружен**: COM5 (USB-JTAG `303A:1001`) |
| МинGW g++ | установлен (winget) — нужен для `pio test -e native*` |

**Плата:** 1 подключена (COM5, `node_id=0xE808`), прошита и работает.
Ещё 2 не подключены.

---

## Порядок работы (где мы сейчас)

Выполнено:

1. ✅ Локальный сервер и каркас клиента — **без железа**.
2. ✅ Интерфейс на заглушке: сценарий 1→2→3, статусы, панель узлов (этапы 1–8).
3. ✅ Скелет прошивки и `platformio.ini`.
4. ✅ `mesh_core` реализован и покрыт тестами — **29/29 PASSED**.
5. ✅ Сценарий 1→2→3 проверен в симуляторе (`test_sim`).
6. ✅ H1: плата прошита, Serial работает, `espnow` + `ble` поднимаются.

Дальше:

7. ⏳ **H2** — подключение клиента к BLE-шлюзу из Chrome (сейчас).
8. Две платы: ESP-NOW, затем сосуществование BLE + ESP-NOW (самый рискованный шаг).
9. Три платы: демонстрация 1→2→3 (H3).
10. Этап 9 клиента — реальный `ble.js`.

Клиент и прошивка независимы: связаны только контрактом
[BLE_API](protocol/BLE_API.md). Подробнее —
[WEB_CLIENT_PLAN](docs/WEB_CLIENT_PLAN.md) §8,
[TEST_PLAN](docs/TEST_PLAN.md).

### Как запускать и проверять

Подробная инструкция — [docs/INSTRUCTIONS.md](docs/INSTRUCTIONS.md).
Кратко:

- **Тесты (без железа):** `pio test -e native_packet -e native_relay -e native_nodes -e native_sim` из `firmware/`.
- **Сборка прошивки:** `pio run -e esp32c3` из `firmware/`.
- **Прошивка платы:** `pio run -e esp32c3 -t upload --upload-port COM5` (требует согласования!).
- **Логи платы:** `pio device monitor --port COM5 --baud 115200`.
- **Клиент:** сервер поднять (Python из DSH-runtime, `docs/INSTRUCTIONS.md` §5.1), затем `http://localhost:5500/`.

---

## Ключевые решения

- **Source routing.** Путь накапливается в пакете, ACK идёт по обратному
  пути. Даёт показ маршрута и не удваивает трафик.
- **ID сообщения** = `node_id + session_id + seq`. Не хэш текста и времени:
  часы узлов не синхронизированы, а одинаковый текст может отправляться
  повторно.
- **Одна прошивка на все платы.** `node_id` берётся из MAC, имя хранится в
  NVS. Отдельной сборки под шлюз нет.
- **Логика отделена от железа.** `lib/mesh_core/` не включает Arduino —
  иначе её нельзя протестировать на ПК.
- **Имя привязано к плате, не к приложению.** Пересел на другой
  компьютер — имя то же. Личность живёт в устройстве.
- **Без фрагментации в v1.** Клиент ограничивает ввод 179 байтами и
  показывает счётчик. Подробнее — [BLE_API](protocol/BLE_API.md) §3.2.
- **Гибридная адресация.** По умолчанию «всем», плюс выбор конкретного
  узла. Групп в v1 нет: членство хранить негде.
- **Клиент — веб.** Закрывает Windows, Linux и Android; iOS не требуется,
  а Flutter добавлял бы только его.

---

## Ограничения платформы

| Браузер | Статус |
|---|---|
| Chrome/Edge, Windows | ✅ Работает |
| Chrome, Linux | ⚠️ Нужен флаг `#enable-experimental-web-platform-features` |
| Chrome, macOS | ❓ `[ПРОВЕРИТЬ]` — не подтверждено, проверка на Mac |
| Chrome, Android | ✅ Работает, отладка сложнее |
| Safari / iOS | ❌ Web Bluetooth отсутствует |
| Firefox | ❌ Web Bluetooth отсутствует |

Web Bluetooth требует защищённого контекста: `file://` не подходит,
открытие `index.html` двойным кликом не заработает. Нужен
`http://localhost` или HTTPS.

**Для Android это важнее, чем кажется:** телефон не может открыть
`localhost` компьютера. Нужен либо адрес машины в локальной сети, либо
HTTPS-туннель. При этом `http://` по IP-адресу **не** считается
защищённым контекстом, поэтому нужен HTTPS.

Подключение требует клика пользователя — автоподключение при загрузке
страницы невозможно. Подробнее — в [BLE_API](protocol/BLE_API.md) §1
и [WEB_CLIENT_PLAN](docs/WEB_CLIENT_PLAN.md) §2.
