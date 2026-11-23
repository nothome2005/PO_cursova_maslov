@echo off
setlocal enabledelayedexpansion
title Parallel Search Engine (C++20 ^& Web UI)

echo ==============================================================================
echo    Паралельна пошукова система з LLM Query Expansion (C++20)
echo    Курсова робота з дисципліни "Паралельні обчислення"
echo ==============================================================================
echo.

:: 1. Перевірка роботи Docker
echo [*] Перевірка статусу Docker...
docker info >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [!] Docker Desktop не запущено. Будь ласка, запустіть Docker і спробуйте знову.
    pause
    exit /b 1
)

:: 2. Запуск контейнерів
echo [*] Запуск пошукового сервера C++ та Ollama LLM (docker compose up -d)...
docker compose up -d
if %ERRORLEVEL% NEQ 0 (
    echo [!] Помилка під час запуску docker compose.
    pause
    exit /b %ERRORLEVEL%
)

:: 3. Очікування готовності сервера
echo [*] Очікування ініціалізації сервера та побудови індексу...
set /a retries=0
:wait_loop
curl -s http://localhost:8088/api/health >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [v] Сервер успішно запущено і він готовий до роботи!
    goto ready
)
set /a retries+=1
if %retries% GEQ 30 (
    echo [!] Сервер все ще запускається (можливо триває завантаження моделі або індексація).
    goto ready
)
timeout /t 2 /nobreak >nul
goto wait_loop

:ready
echo.
echo ==============================================================================
echo    СИСТЕМА ГОТОВА ДО РОБОТИ:
echo    - Web UI:      http://localhost:8088
echo    - REST API:    http://localhost:8088/api/health
echo    - Ollama LLM:  http://localhost:11434
echo ==============================================================================
echo.
echo Приклади використання CLI-клієнта:
echo   docker exec -it search_engine_server /app/search "parallel computing"
echo   docker exec -it search_engine_server /app/search -m llm "neural network"
echo   docker exec -it search_engine_server /app/search --stats
echo.

:: Відкриття веб-інтерфейсу у браузері
start http://localhost:8088

echo Натисніть будь-яку клавішу для зупинки контейнерів або закрийте це вікно...
pause >nul

echo [*] Зупинка сервісів...
docker compose down
echo [v] Сервіси зупинено.
