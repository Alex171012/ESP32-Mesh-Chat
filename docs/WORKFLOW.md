# WORKFLOW — как мы работаем с репозиторием

Документ для человека и для нового чата. Описывает репозиторий,
правила коммитов, порядок работы и точку входа в задачи.

---

## 1. Репозиторий

| Параметр | Значение |
|---|---|
| Проект | ESP32-C3 Mesh Chat |
| Локальный путь | `C:\Users\Aleksandr\Desktop\Project_ESPchat` |
| Remote | `https://github.com/Alex171012/ESP32-Mesh-Chat.git` |
| Ветка | `main` |
| Видимость | публичный |
| Аккаунт | `Alex171012` |
| Подпись коммитов | `Alex171012 <Alex171012@users.noreply.github.com>` |
| Аутентификация | `gh` CLI, вход через браузер |

Связанные документы: план проекта — `ESP32_C3_Mesh_Chat_Project_Plan.md`,
архитектура — `docs/ARCHITECTURE.md`, тесты — `docs/TEST_PLAN.md`,
клиент — `docs/WEB_CLIENT_PLAN.md`, протоколы — `protocol/`.

---

## 2. Правило коммитов

**Коммит и пуш после каждого логического блока.**

Логический блок — это законченное изменение, а не каждое нажатие
клавиши. Примеры блоков:

- дописан раздел документа;
- добавлен модуль и он собирается;
- исправлена ошибка;
- обновлён план после решения.

Автосохранение на каждое изменение файла **не делается** — история
превратилась бы в мусор.

**Пуш обязателен сразу после коммита.** Локальные коммиты, которые
лежат непушенными, — плохая идея: их легко потерять и о них легко
забыть.

---

## 3. Порядок действий

### 3.1. Начало работы

```powershell
$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")
Set-Location "C:\Users\Aleksandr\Desktop\Project_ESPchat"
git status
git pull
```

`git pull` перед началом — чтобы не работать на устаревшей версии.

### 3.2. Проверка перед коммитом

Секреты и MAC-адреса в публичном репозитории недопустимы: история
вечная, и удалённый файл остаётся доступен по прямой ссылке на коммит.

```powershell
git diff --cached | Select-String -Pattern "gho_|ghp_|github_pat_|AKIA|password\s*[:=]|api[_-]?key\s*[:=]|token\s*[:=]\s*[A-Za-z0-9]"
```

Проверка MAC-адресов (реальные MAC плат попадать в репозиторий не должны,
так как `node_id` выводится из MAC):

```powershell
git diff --cached | Select-String -Pattern "([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}"
```

Обе команды должны ничего не найти. Если нашли — убрать из индекса.

### 3.3. Коммит

**Важно:** PowerShell не поддерживает heredoc, а `git commit -m` с
русским текстом в командной строке ломает кодировку. Поэтому сообщение
пишется в файл.

1. Записать сообщение в `.git\COMMIT_MSG_TMP.txt`
2. Выполнить:

```powershell
git add -A
git commit -q -F ".git\COMMIT_MSG_TMP.txt"
Remove-Item ".git\COMMIT_MSG_TMP.txt"
git push origin main
```

Файл лежит в `.git\`, поэтому в индекс не попадает.

**Вывод git идёт в stderr.** PowerShell показывает его как
`NativeCommandError` — это **не** ошибка. Признак успеха — `exit code: 0`
и строка вида `main -> main`.

**`push` требует полного доступа к системе.** Git получает учётные данные
через внешний процесс (`gh auth git-credential`). В ограниченном режиме
DSH этот процесс не запускается, и пуш падает так:

```text
fatal error - couldn't create signal pipe, Win32 error 5
error: failed to execute prompt script (exit code 66)
fatal: could not read Username for 'https://github.com'
```

**Это не поломка конфига.** Проверка `gh auth git-credential get`
в том же окружении выдаёт токен — значит учётные данные в порядке, а
виноват запрет на запуск внешних процессов. Не надо править `helper`,
`remote` или вписывать токен в URL: нужен только полный доступ.

Отдельная ловушка: `http.sslBackend=schannel` (значение по умолчанию в
системном конфиге Git) ломает TLS на этой машине с ошибкой
`schannel: AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS`.
Лечится один раз на репозиторий:

```powershell
git config http.sslBackend openssl
```

Параметр хранится в `.git\config`, то есть в репозиторий не попадает и
при клонировании его нужно задать заново.

### 3.4. Проверка результата

```powershell
git log --oneline -5
git status
git rev-parse HEAD
git rev-parse origin/main
```

HEAD и `origin/main` должны совпадать, рабочее дерево — быть чистым.

---

## 4. Требования к окружению

| Что | Где | Примечание |
|---|---|---|
| Git | `C:\Program Files\Git\cmd` | 2.56.0.4, в PATH |
| GitHub CLI | `C:\Program Files\GitHub CLI\gh.exe` | 2.104.0, вход выполнен |
| PlatformIO | `C:\Users\Aleksandr\.platformio\penv\Scripts\pio.exe` | **не в PATH**, вызывать полным путём |
| VS Code | `%LOCALAPPDATA%\Programs\Microsoft VS Code\bin\code.cmd` | расширения: C/C++, PlatformIO IDE |
| Node.js | не установлен | нужен для локального сервера клиента |

**PlatformIO требует:** `$env:PYTHONIOENCODING = "utf-8"` перед запуском,
иначе падает с `UnicodeEncodeError`.

**Файлы конфигурации писать без BOM.** PowerShell 5.1 через
`Set-Content -Encoding UTF8` добавляет BOM, и PlatformIO не читает
`platformio.ini`. Правильно:

```powershell
[System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
```

---

## 5. Правила работы

### 5.1. Что требует согласования

| Действие | Согласование |
|---|---|
| Правка документов | не требует |
| Написание кода | требует явного согласия |
| **Прошивка плат** | **отдельное согласование каждый раз** (план §9.3) |
| Установка ПО | требует |
| Смена решений v1 | требует |

### 5.2. Принципы

- **Документы — источник истины.** Если код расходится с
  `protocol/BLE_API.md`, прав код или документ, но не оба.
- **Числовые параметры помечать `[ПРОВЕРИТЬ]`**, пока не проверены на
  железе. Их много: TTL, тайм-ауты, MTU, размеры кэша.
- **Общение на русском**, имена файлов и код — латиницей.
- **Проверять состояние на диске**, а не статус фонового процесса.
- **Не доверять `running`** у фоновой задачи: она может сообщать о
  работе, когда всё давно завершилось.

---

## 6. Начало нового чата по этапу

Репозиторий самодостаточен: всё нужное для старта лежит в нём.
Формулировка для нового чата:

> Прочитай `docs/WORKFLOW.md`, `README.md` и `ESP32_C3_Mesh_Chat_Project_Plan.md`.
> Затем сделаем этап: **<название>** из `docs/WEB_CLIENT_PLAN.md` §8
> (или этап прошивки из плана).

Что прочитать под конкретную задачу:

| Этап | Документы |
|---|---|
| Клиент, интерфейс | `docs/WEB_CLIENT_PLAN.md`, `protocol/BLE_API.md` §4, §6–7 |
| Клиент, протокол | `protocol/BLE_API.md` §3–4 |
| Прошивка, mesh | `protocol/MESH_PROTOCOL.md`, `docs/ARCHITECTURE.md` |
| Прошивка, BLE | `protocol/BLE_API.md`, `docs/ARCHITECTURE.md` §5 |
| Тесты | `docs/TEST_PLAN.md` |

**Что сообщить в новом чате:** какое железо подключено, есть ли
USB-кабель с передачей данных, установлен ли Node.js. Это переменные
величины, и в репозитории они не отражены.

---

## 7. Текущее состояние

| Что | Состояние |
|---|---|
| Документы | готовы |
| Контракт BLE | зафиксирован (`BLE_API.md` §6–7) |
| Протокол mesh | зафиксирован (`MESH_PROTOCOL.md` §7.1) |
| Реализация | **не начата** |
| Платы | 3 шт., не подключены |
| USB-serial | не обнаружен, прошивка невозможна |

**Следующий шаг:** этап 1 клиента — каркас (`WEB_CLIENT_PLAN.md` §8).
Для него нужен локальный сервер, то есть Node.js.
