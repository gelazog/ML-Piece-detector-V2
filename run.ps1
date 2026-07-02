# run.ps1 — Verifica los requisitos de PC Inspector, instala lo que falte,
# compila si hace falta y lanza la aplicación.
#
# Uso:
#   .\run.ps1            verifica + compila si falta + ejecuta
#   .\run.ps1 -Rebuild   fuerza reconfigurar y recompilar
#   .\run.ps1 -NoRun     solo verifica/instala/compila, sin lanzar la app
param(
    [switch]$Rebuild,
    [switch]$NoRun
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$msys = 'C:\msys64'
$mingwBin = Join-Path $msys 'mingw64\bin'
$bash = Join-Path $msys 'usr\bin\bash.exe'
$exe = Join-Path $root 'build\release\pc_inspector.exe'

# Requisitos de compilación y ejecución (MSYS2/MinGW64, todo precompilado).
$packages = @(
    'mingw-w64-x86_64-gcc',
    'mingw-w64-x86_64-cmake',
    'mingw-w64-x86_64-ninja',
    'mingw-w64-x86_64-qt6-base',
    'mingw-w64-x86_64-opencv'
)

function Write-Step([string]$message) { Write-Host "==> $message" -ForegroundColor Cyan }
function Write-Ok([string]$message) { Write-Host "    $message" -ForegroundColor Green }

# --- 1. MSYS2 ---
if (-not (Test-Path $bash)) {
    Write-Step 'MSYS2 no encontrado: instalando con winget (puede tardar unos minutos)...'
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        throw 'winget no está disponible. Instala MSYS2 manualmente desde https://www.msys2.org y vuelve a ejecutar.'
    }
    winget install --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements --disable-interactivity --silent
    if (-not (Test-Path $bash)) {
        throw "La instalación de MSYS2 no dejó $msys. Revisa la salida de winget."
    }
    Write-Step 'Actualizando la base de MSYS2...'
    & $bash -lc 'pacman -Syu --noconfirm'
    & $bash -lc 'pacman -Su --noconfirm'
} else {
    Write-Ok "MSYS2 presente en $msys"
}

# --- 2. Paquetes ---
Write-Step 'Verificando paquetes requeridos...'
# stderr se descarta dentro de bash para no ensuciar la consola de PowerShell.
$installedOutput = & $bash -lc "pacman -Q $($packages -join ' ') 2>/dev/null"
$installed = @($installedOutput | ForEach-Object { ($_ -split ' ')[0] })
$missing = @($packages | Where-Object { $installed -notcontains $_ })

if ($missing.Count -gt 0) {
    Write-Step "Instalando paquetes faltantes: $($missing -join ', ')"
    & $bash -lc "pacman -S --noconfirm --needed $($missing -join ' ')"
    if ($LASTEXITCODE -ne 0) { throw 'pacman falló instalando los paquetes.' }
} else {
    Write-Ok "Los $($packages.Count) paquetes requeridos están instalados."
}

# --- 3. Compilar ---
$env:PATH = "$mingwBin;$env:PATH"
if ($Rebuild -or -not (Test-Path $exe)) {
    Write-Step 'Configurando y compilando (Release)...'
    Push-Location $root
    try {
        cmake --preset mingw-release
        if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración de CMake.' }
        cmake --build --preset mingw-release
        if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación.' }
    } finally {
        Pop-Location
    }
    Write-Ok 'Compilación correcta.'
} else {
    Write-Ok "Binario ya compilado: $exe (usa -Rebuild para forzar)"
}

# --- 4. Ejecutar ---
if (-not $NoRun) {
    Write-Step 'Lanzando PC Inspector...'
    # El PATH con mingw64\bin es necesario para las DLL de Qt/OpenCV.
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
    Write-Ok 'Aplicación lanzada.'
}
