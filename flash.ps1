param(
    [Parameter(Mandatory = $true, HelpMessage = "Porta COM (ex: COM4)")]
    [string]$Port,
    [int]$Baud = 460800
)

$ProjectRoot = Split-Path $PSCommandPath -Parent
$Binaries = @(
    @{ offset = "0x1000";  file = "build/bootloader/bootloader.bin" },
    @{ offset = "0x8000";  file = "build/partition_table/partition-table.bin" },
    @{ offset = "0x1d000"; file = "build/ota_data_initial.bin" },
    @{ offset = "0x20000"; file = "build/esp_matter_bridge.bin" }
)

Write-Host "ESP-Matter Bridge Flasher" -ForegroundColor Cyan
Write-Host "Porta: $Port" -ForegroundColor Gray
Write-Host ""

foreach ($bin in $Binaries) {
    $path = Join-Path $ProjectRoot $bin.file
    if (-not (Test-Path $path)) {
        Write-Host "ERRO: $path nao encontrado" -ForegroundColor Red
        exit 1
    }
    Write-Host "  OK: $($bin.file)" -ForegroundColor Green
}

$argsList = @(
    "--chip", "esp32",
    "-b", $Baud,
    "--port", $Port,
    "write_flash",
    "--flash_mode", "dio",
    "--flash_size", "4MB",
    "--flash_freq", "40m"
)

foreach ($bin in $Binaries) {
    $argsList += $bin.offset
    $argsList += (Join-Path $ProjectRoot $bin.file)
}

Write-Host "`nFlashando..." -ForegroundColor Yellow
$global:LASTEXITCODE = 0
& esptool $argsList

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nFlash concluido com sucesso!" -ForegroundColor Green
} else {
    Write-Host "`nFalha no flash (codigo $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}
