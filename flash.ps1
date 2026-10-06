#!/usr/bin/env pwsh
# flash.ps1 — Flasha ESP32-C3 lux-sensor firmware
# Kräver: Python 3 installerat och tillgängligt som 'python' i PATH
#
# Användning:
#   .\flash.ps1              # visar lista på COM-portar, frågar vilken
#   .\flash.ps1 -Port COM4   # flasha direkt på COM4
#   .\flash.ps1 -Port COM4 -Erase  # radera flash först (tvingar captive portal)

param(
    [string]$Port = "",
    [switch]$Erase
)

$BinDir = "$PSScriptRoot\bin"
$MergedBin = "$BinDir\lux_sensor_c3_full_0x0.bin"

# --- Kontrollera Python -------------------------------------------------------
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "FEL: 'python' hittades inte i PATH." -ForegroundColor Red
    Write-Host "Installera Python 3 fran https://python.org och bocka i 'Add to PATH'." -ForegroundColor Yellow
    exit 1
}

# --- Installera esptool om det saknas ----------------------------------------
python -c "import esptool" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Installerar esptool..." -ForegroundColor Cyan
    python -m pip install esptool --quiet
}

# --- Visa tillgangliga COM-portar --------------------------------------------
Write-Host ""
Write-Host "Tillgangliga COM-portar:" -ForegroundColor Cyan
$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
if ($ports.Count -eq 0) {
    Write-Host "  (inga portar hittades — anslut ESP32-C3 via USB)" -ForegroundColor Yellow
} else {
    $ports | ForEach-Object { Write-Host "  $_" }
}
Write-Host ""

# --- Fraga om port om ej angiven ---------------------------------------------
if ($Port -eq "") {
    $Port = Read-Host "Ange COM-port (t.ex. COM4)"
}
if ($Port -eq "") { Write-Host "Ingen port angiven. Avbryter." -ForegroundColor Red; exit 1 }

# --- Kontrollera att bin-filen finns -----------------------------------------
if (-not (Test-Path $MergedBin)) {
    Write-Host "FEL: Hittade inte $MergedBin" -ForegroundColor Red
    Write-Host "Kör skriptet från rotmappen i repot (dar bin\ finns)." -ForegroundColor Yellow
    exit 1
}

Write-Host "Flashar $MergedBin pa $Port ..." -ForegroundColor Green
Write-Host "(Hall BOOT nedtryckt + tryck RESET om enheten inte svarar)" -ForegroundColor Yellow
Write-Host ""

# --- Radera flash om -Erase --------------------------------------------------
if ($Erase) {
    Write-Host "Raderar flash..." -ForegroundColor Cyan
    python -m esptool --chip esp32c3 --port $Port --baud 921600 erase_flash
}

# --- Flasha ------------------------------------------------------------------
python -m esptool `
    --chip esp32c3 `
    --port $Port `
    --baud 921600 `
    --before default_reset `
    --after hard_reset `
    write_flash `
    --flash_mode dio `
    --flash_freq 80m `
    --flash_size 4MB `
    0x0 $MergedBin

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Klart! Enheten startar om." -ForegroundColor Green
    Write-Host "Om WiFi-uppgifter saknas: anslut till WiFi 'LuxSensor-XXXXXX' och oppna 192.168.4.1"
} else {
    Write-Host ""
    Write-Host "Flashning misslyckades (kod $LASTEXITCODE)." -ForegroundColor Red
    Write-Host "Tips: hall BOOT + tryck RESET precis innan du kor skriptet igen."
}
