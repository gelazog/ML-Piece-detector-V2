# run.ps1 — Verifica los requisitos de PC Inspector, instala lo que falte,
# compila si hace falta y lanza la aplicación. Si algo no puede instalarse
# automáticamente, dice exactamente qué falta y cómo resolverlo a mano.
#
# Uso:
#   .\run.ps1            verifica + compila si falta + ejecuta
#   .\run.ps1 -Rebuild   fuerza reconfigurar y recompilar
#   .\run.ps1 -NoRun     solo verifica/instala/compila, sin lanzar la app
#   .\run.ps1 -Test      además corre la suite de tests (ctest)
param(
    [switch]$Rebuild,
    [switch]$NoRun,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = $PSScriptRoot
$msys = 'C:\msys64'
$ucrtBin = Join-Path $msys 'ucrt64\bin'
$bash = Join-Path $msys 'usr\bin\bash.exe'
$exe = Join-Path $root 'build\release\pc_inspector.exe'

# Requisitos de compilación y ejecución (MSYS2/UCRT64, todo precompilado).
# protobuf aporta el protoc con el que se prepara el modelo de embeddings.
$packages = @(
    'mingw-w64-ucrt-x86_64-gcc',
    'mingw-w64-ucrt-x86_64-cmake',
    'mingw-w64-ucrt-x86_64-ninja',
    'mingw-w64-ucrt-x86_64-qt6-base',
    'mingw-w64-ucrt-x86_64-opencv',
    'mingw-w64-ucrt-x86_64-onnxruntime',
    'mingw-w64-ucrt-x86_64-protobuf',
    'mingw-w64-ucrt-x86_64-sqlite3'
)

# Modelo de embeddings (EfficientNet-Lite4 del zoo oficial de ONNX; ver README).
$modelsDir = Join-Path $root 'models'
$rawModel = Join-Path $modelsDir 'efficientnet-lite4-11.onnx'
$model = Join-Path $modelsDir 'embedding_model.onnx'
$modelUrl = 'https://github.com/onnx/models/raw/main/validated/vision/classification/efficientnet-lite4/model/efficientnet-lite4-11.onnx'

function Write-Step([string]$message) { Write-Host "==> $message" -ForegroundColor Cyan }
function Write-Ok([string]$message) { Write-Host "    $message" -ForegroundColor Green }
function Write-Warn([string]$message) { Write-Host "    $message" -ForegroundColor Yellow }

# Corta la ejecución explicando qué falta y cómo resolverlo a mano.
function Stop-Missing([string]$what, [string[]]$howTo) {
    Write-Host ''
    Write-Host "X FALTA: $what" -ForegroundColor Red
    Write-Host '  Cómo resolverlo manualmente:' -ForegroundColor Red
    foreach ($line in $howTo) {
        Write-Host "    $line"
    }
    Write-Host ''
    exit 1
}

# --- 1. MSYS2 ---
if (-not (Test-Path $bash)) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        Stop-Missing 'MSYS2 (y no hay winget para instalarlo automáticamente)' @(
            '1. Descarga el instalador desde https://www.msys2.org',
            "2. Instálalo en la ruta por defecto ($msys)",
            '3. Vuelve a ejecutar .\run.ps1'
        )
    }
    Write-Step 'MSYS2 no encontrado: instalando con winget (puede tardar unos minutos)...'
    winget install --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements --disable-interactivity --silent
    if (-not (Test-Path $bash)) {
        Stop-Missing 'MSYS2 (winget no lo dejó instalado)' @(
            '1. Revisa la salida de winget de arriba',
            '2. O instálalo manualmente desde https://www.msys2.org en C:\msys64',
            '3. Vuelve a ejecutar .\run.ps1'
        )
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
    if ($LASTEXITCODE -ne 0) {
        Stop-Missing "paquetes de MSYS2: $($missing -join ', ')" @(
            '1. Abre la terminal "MSYS2 UCRT64" (C:\msys64\ucrt64.exe)',
            "2. Ejecuta:  pacman -S --needed $($missing -join ' ')",
            '   (si pacman dice que la BD está bloqueada y no hay otro pacman',
            '    corriendo, borra C:\msys64\var\lib\pacman\db.lck)',
            '3. Vuelve a ejecutar .\run.ps1'
        )
    }
} else {
    Write-Ok "Los $($packages.Count) paquetes requeridos están instalados."
}

# --- 3. Compilar ---
$env:PATH = "$ucrtBin;$env:PATH"
# Recompilar si se fuerza (-Rebuild), si no hay binario, o si el código fuente
# es más nuevo que el binario (evita lanzar una versión vieja tras git pull).
$needsBuild = $Rebuild -or (-not (Test-Path $exe))
if (-not $needsBuild) {
    $exeTime = (Get-Item $exe).LastWriteTime
    $newest = Get-ChildItem -Path (Join-Path $root 'src'), (Join-Path $root 'tests') `
        -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $cmakeLists = Get-Item (Join-Path $root 'CMakeLists.txt') -ErrorAction SilentlyContinue
    if (($newest -and $newest.LastWriteTime -gt $exeTime) -or
        ($cmakeLists -and $cmakeLists.LastWriteTime -gt $exeTime)) {
        Write-Step 'El código cambió desde la última compilación: recompilando…'
        $needsBuild = $true
    }
}
if ($needsBuild) {
    Write-Step 'Configurando y compilando (Release)...'
    Push-Location $root
    try {
        cmake --preset mingw-release
        if ($LASTEXITCODE -ne 0) {
            Stop-Missing 'configuración de CMake (falló)' @(
                '1. Revisa los errores de CMake de arriba',
                '2. Si cambiaste de toolchain, borra la carpeta build\ y reintenta',
                '3. Manualmente: $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"; cmake --preset mingw-release'
            )
        }
        cmake --build --preset mingw-release
        if ($LASTEXITCODE -ne 0) {
            Stop-Missing 'compilación (falló)' @(
                '1. Revisa los errores del compilador de arriba',
                '2. Manualmente: $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"; cmake --build --preset mingw-release'
            )
        }
    } finally {
        Pop-Location
    }
    Write-Ok 'Compilación correcta.'
} else {
    Write-Ok "Binario ya compilado: $exe (usa -Rebuild para forzar)"
}

# --- 4. Modelo de embeddings (opcional: sin él la app inspecciona solo con
#        herramientas geométricas, sin comparación de apariencia) ---
if (-not (Test-Path $model)) {
    if (-not (Test-Path $modelsDir)) { New-Item -ItemType Directory $modelsDir | Out-Null }
    $downloadOk = Test-Path $rawModel
    if (-not $downloadOk) {
        Write-Step 'Descargando modelo ONNX (~49 MB)...'
        try {
            Invoke-WebRequest -Uri $modelUrl -OutFile $rawModel -UseBasicParsing
            $downloadOk = $true
        } catch {
            Write-Warn 'No se pudo descargar el modelo (¿sin internet?). La app funcionará'
            Write-Warn 'sin comparación de apariencia. Para habilitarla después:'
            Write-Warn "  1. Descarga: $modelUrl"
            Write-Warn "  2. Guárdalo como: $rawModel"
            Write-Warn '  3. Vuelve a ejecutar .\run.ps1'
        }
    }
    if ($downloadOk) {
        Write-Step 'Preparando modelo de embeddings (recorte del clasificador)...'
        $prepareTool = Join-Path $root 'build\release\prepare_model.exe'
        if (Test-Path $prepareTool) {
            & $prepareTool $rawModel $model
        }
        if (-not (Test-Path $model)) {
            Write-Warn 'No se pudo recortar; se usará el modelo completo (softmax).'
            Copy-Item $rawModel $model
        }
    }
} else {
    Write-Ok 'Modelo de embeddings presente.'
}

# --- 5. Tests (opcional) ---
if ($Test) {
    Write-Step 'Ejecutando la suite de tests...'
    Push-Location $root
    try {
        ctest --preset mingw-release
        if ($LASTEXITCODE -ne 0) {
            Write-Host 'X Hay tests fallando: revisa la salida de arriba.' -ForegroundColor Red
            exit 1
        }
    } finally {
        Pop-Location
    }
    Write-Ok 'Todos los tests pasaron.'
}

# --- 6. Ejecutar ---
if (-not $NoRun) {
    Write-Step 'Lanzando PC Inspector...'
    # El PATH con ucrt64\bin es necesario para las DLL de Qt/OpenCV/onnxruntime.
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
    Write-Ok 'Aplicación lanzada.'
}

Write-Host ''
Write-Ok 'Todo listo.'
