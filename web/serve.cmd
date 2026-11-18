@echo off
rem Локальный сервер для веб-клиента.
rem Web Bluetooth требует secure context: file:// не подходит (BLE_API.md §1).
rem Вариант 1 (если установлен Python):
python -m http.server 5500 --directory "%~dp0"
rem Вариант 2 (если установлен Node.js):
rem npx serve -l 5500 "%~dp0"
rem Вариант 3: расширение Live Server в VS Code (правый клик по index.html).
pause