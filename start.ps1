# Parallel Search Engine Launcher (PowerShell)
$Host.UI.RawUI.WindowTitle = "Parallel Search Engine (C++20 & Web UI)"

Write-Host "==============================================================================" -ForegroundColor DarkCyan
Write-Host "   Паралельна пошукова система з LLM Query Expansion (C++20)" -ForegroundColor Cyan
Write-Host "   Курсова робота з дисципліни 'Паралельні обчислення'" -ForegroundColor DarkGray
Write-Host "==============================================================================" -ForegroundColor DarkCyan
Write-Host ""

# 1. Перевірка Docker
Write-Host "[*] Перевірка статусу Docker..." -ForegroundColor Yellow
docker info >$null 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] Docker Desktop не запущено. Будь ласка, запустіть Docker і спробуйте знову." -ForegroundColor Red
    pause
    exit 1
}

# 2. Запуск контейнерів
Write-Host "[*] Запуск C++ Search Engine та Ollama LLM у фоні..." -ForegroundColor Yellow
docker compose up -d
if ($LASTEXITCODE -ne 0) {
    Write-Host "[!] Помилка під час запуску docker compose." -ForegroundColor Red
    pause
    exit 1
}

# 3. Очікування готовності сервера
Write-Host "[*] Очікування ініціалізації сервера на порті 8088..." -ForegroundColor Yellow
$retries = 0
while ($retries -lt 30) {
    try {
        $resp = Invoke-RestMethod -Uri "http://localhost:8088/api/health" -TimeoutSec 2 -ErrorAction Stop
        if ($resp.status -eq "ok") {
            Write-Host "[v] C++ Search Engine успішно запущено і він готовий до роботи!" -ForegroundColor Green
            break
        }
    } catch {
        Start-Sleep -Seconds 2
        $retries++
    }
}

Write-Host ""
Write-Host "==============================================================================" -ForegroundColor DarkCyan
Write-Host "   СИСТЕМА ГОТОВА ДО РОБОТИ:" -ForegroundColor Green
Write-Host "   - Web UI:      http://localhost:8088" -ForegroundColor White
Write-Host "   - REST API:    http://localhost:8088/api/health" -ForegroundColor White
Write-Host "   - Ollama LLM:  http://localhost:11434" -ForegroundColor White
Write-Host "==============================================================================" -ForegroundColor DarkCyan
Write-Host ""
Write-Host "Приклади використання CLI-клієнта:" -ForegroundColor Yellow
Write-Host "  docker exec -it search_engine_server /app/search ""parallel computing""" -ForegroundColor Cyan
Write-Host "  docker exec -it search_engine_server /app/search -m llm ""neural network""" -ForegroundColor Cyan
Write-Host "  docker exec -it search_engine_server /app/search --stats" -ForegroundColor Cyan
Write-Host ""

# Відкриття браузера
Start-Process "http://localhost:8088"

$stop = Read-Host "Зупинити контейнери Docker? (y/N)"
if ($stop -eq "y" -or $stop -eq "Y") {
    Write-Host "[*] Зупинка сервісів docker compose..." -ForegroundColor Yellow
    docker compose down
    Write-Host "[v] Сервіси зупинено." -ForegroundColor Green
}
