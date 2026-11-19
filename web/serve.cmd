@echo off
rem Локальный сервер для веб-клиента.
rem Web Bluetooth требует secure context: file:// не подходит (BLE_API.md §1).
rem
rem Системного python НЕТ (python в PATH — заглушка Microsoft Store).
rem Используем bundled Python из DSH-runtime. Если его нет — варианты ниже.

set "PY=C:\Users\Aleksandr\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"

if exist "%PY%" (
    echo Serving on http://localhost:5500/  (Ctrl+C to stop)
    "%PY%" -m http.server 5500 --directory "%~dp0"
    goto :done
)

rem Вариант 2 (если установлен настоящий Python):
rem python -m http.server 5500 --directory "%~dp0"

rem Вариант 3: расширение Live Server в VS Code (правый клик по index.html).

echo Не найден Python. См. docs/INSTRUCTIONS.md ^> 5.1
:done
pause