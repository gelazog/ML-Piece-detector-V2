<#
.SYNOPSIS
    Reanuda el trabajo automático en PC Inspector cuando vuelve a haber cupo.

.DESCRIPTION
    Nació de una petición concreta: «que cada vez que se reinicien los tokens
    vuelvas a trabajar automáticamente si Visual Studio Code está abierto».

    POR QUÉ CADA HORA Y NO A LA HORA DEL RESET.

    El cupo no se reinicia a una hora fija del reloj: va en ventanas móviles que
    empiezan con el primer mensaje de cada sesión, así que la hora de reinicio
    cambia de un día para otro. Predecirla obliga a acertar, y fallar significa
    quedarse parado horas.

    Así que esto NO predice: lo intenta cada hora. Si no hay cupo, Claude sale
    con error en segundos, se anota en el registro y se vuelve a intentar a la
    hora siguiente. Cuesta una llamada fallida por hora y no se pierde ninguna
    ventana.

    LAS CUATRO BARRERAS, y por qué cada una.

    1. Visual Studio Code tiene que estar abierto. Es lo que se pidió, y además
       es la señal más honesta de «estoy delante del ordenador»: si el usuario
       cerró el editor, no quiere que nadie le toque el repositorio.
    2. Un cerrojo. Si una ejecución se alarga más de una hora, la siguiente no
       arranca encima. Dos Claude escribiendo el mismo fichero es cómo se pierde
       trabajo.
    3. No arranca si ya hay una sesión de Claude abierta. La tarea salta a la
       hora en punto, y esa es justo la hora a la que alguien puede estar
       trabajando a mano en el mismo repositorio.
    4. NO PUBLICA salvo que se le diga con -Publicar. Commitea en local y para.
       Empujar a main sin que nadie mire es un paso más grande que trabajar sin
       que nadie mire, y son dos decisiones distintas.

.PARAMETER Instalar
    Registra la tarea programada horaria y sale.

.PARAMETER Desinstalar
    Quita la tarea programada y sale.

.PARAMETER Publicar
    Permite hacer push a main al terminar. Sin esto, solo commitea en local.

.PARAMETER Ahora
    Ignora la comprobación de Visual Studio Code y ejecuta ya. Para probar.

.PARAMETER PresupuestoUsd
    Tope de gasto por ejecución. Por defecto 5.

.EXAMPLE
    .\reanudar.ps1 -Instalar
    .\reanudar.ps1 -Ahora
    .\reanudar.ps1 -Desinstalar
#>
[CmdletBinding()]
param(
    [switch]$Instalar,
    [switch]$Desinstalar,
    [switch]$Publicar,
    [switch]$Ahora,
    [double]$PresupuestoUsd = 5.0
)

$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $MyInvocation.MyCommand.Path
$registro = Join-Path $raiz '.claude\reanudar.log'
$cerrojo = Join-Path $raiz '.claude\reanudar.lock'
$nombreTarea = 'PCInspector-Reanudar'

function Anotar([string]$texto) {
    $linea = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $texto
    $carpeta = Split-Path -Parent $registro
    if (-not (Test-Path $carpeta)) { New-Item -ItemType Directory -Force $carpeta | Out-Null }
    Add-Content -Path $registro -Value $linea -Encoding utf8
    Write-Host $linea
}

# --- Instalar / desinstalar la tarea ---------------------------------------

if ($Desinstalar) {
    if (Get-ScheduledTask -TaskName $nombreTarea -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $nombreTarea -Confirm:$false
        Anotar "Tarea '$nombreTarea' quitada."
    } else {
        Anotar "No había ninguna tarea '$nombreTarea'."
    }
    exit 0
}

if ($Instalar) {
    $guion = Join-Path $raiz 'reanudar.ps1'
    $argumentos = '-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $guion
    if ($Publicar) { $argumentos += ' -Publicar' }

    $accion = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $argumentos -WorkingDirectory $raiz
    # Cada hora, indefinidamente. La primera a la hora en punto siguiente.
    $inicio = (Get-Date).Date.AddHours((Get-Date).Hour + 1)
    # OJO CON LA DURACIÓN DE LA REPETICIÓN.
    #
    # Lo natural sería [TimeSpan]::MaxValue para decir «indefinidamente», y el
    # programador de tareas lo RECHAZA: genera P99999999DT23H59M59S y responde
    # «valor fuera de intervalo». Se comprobó ejecutándolo. Diez años es
    # indefinido a todos los efectos y sí lo acepta.
    $disparador = New-ScheduledTaskTrigger -Once -At $inicio `
        -RepetitionInterval (New-TimeSpan -Hours 1) `
        -RepetitionDuration (New-TimeSpan -Days 3650)
    # Sin AllowStartIfOnBatteries un portátil desenchufado no ejecutaría nunca,
    # y es justo cuando alguien deja el equipo trabajando solo.
    $ajustes = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries -StartWhenAvailable `
        -ExecutionTimeLimit (New-TimeSpan -Hours 2) -MultipleInstances IgnoreNew

    if (Get-ScheduledTask -TaskName $nombreTarea -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $nombreTarea -Confirm:$false
    }
    try {
        Register-ScheduledTask -TaskName $nombreTarea -Action $accion -Trigger $disparador `
            -Settings $ajustes -ErrorAction Stop `
            -Description 'Reanuda el trabajo de Claude en PC Inspector cuando hay cupo y VS Code está abierto.' | Out-Null
    } catch {
        Anotar ('ERROR al registrar la tarea: {0}' -f $_.Exception.Message)
        exit 1
    }

    # SE COMPRUEBA QUE DE VERDAD ESTÁ, y no es paranoia.
    #
    # La primera versión anotaba «tarea registrada» aunque Register-ScheduledTask
    # hubiera fallado: el error de la API CIM se imprimía y el script seguía como
    # si nada. Quedaba un registro diciendo que estaba instalada y una máquina que
    # no iba a ejecutar nada nunca. Un script que miente sobre haberse instalado
    # es peor que no tener script.
    $comprobacion = Get-ScheduledTask -TaskName $nombreTarea -ErrorAction SilentlyContinue
    if ($null -eq $comprobacion) {
        Anotar 'ERROR: la tarea no aparece después de registrarla.'
        exit 1
    }
    Anotar "Tarea '$nombreTarea' registrada y comprobada ($($comprobacion.State)): cada hora desde $inicio. Publicar=$Publicar."
    Anotar 'Para quitarla: .\reanudar.ps1 -Desinstalar'
    exit 0
}

# --- Barreras ---------------------------------------------------------------

if (-not $Ahora) {
    $vscode = Get-Process -Name 'Code' -ErrorAction SilentlyContinue
    if ($null -eq $vscode) {
        Anotar 'Visual Studio Code no está abierto. No se hace nada.'
        exit 0
    }
}

# Y NO ARRANCAR ENCIMA DE UNA SESIÓN VIVA.
#
# Es el choque más fácil de provocar: la tarea salta a la hora en punto mientras
# alguien está trabajando con Claude en el mismo repositorio, y quedan dos
# escribiendo los mismos ficheros. El cerrojo de abajo solo protege de que esta
# tarea se pise a sí misma; esto protege de la sesión interactiva.
#
# El proceso se llama `claude.exe` — comprobado en la máquina, no supuesto.
if (-not $Ahora) {
    $sesionViva = Get-Process -Name 'claude' -ErrorAction SilentlyContinue
    if ($null -ne $sesionViva) {
        Anotar 'Ya hay una sesión de Claude abierta. No se hace nada.'
        exit 0
    }
}

if (Test-Path $cerrojo) {
    $edad = (Get-Date) - (Get-Item $cerrojo).LastWriteTime
    if ($edad.TotalHours -lt 3) {
        Anotar ('Ya hay una ejecución en marcha desde hace {0:N0} min. No se hace nada.' -f $edad.TotalMinutes)
        exit 0
    }
    # Un cerrojo de más de tres horas es de una ejecución que murió: se pisa.
    Anotar 'Cerrojo caducado (>3 h): se ignora.'
}

$claude = Get-Command claude -ErrorAction SilentlyContinue
if ($null -eq $claude) {
    Anotar 'ERROR: no se encuentra el ejecutable `claude` en el PATH.'
    exit 1
}

# --- El encargo -------------------------------------------------------------
#
# Acotado a UNA cosa por ejecución, y con las reglas del proyecto repetidas
# aquí porque una sesión nueva no recuerda nada: cada ejecución empieza en
# frío. Lo que sí puede leer es CONTEXTO.md, que es justo para esto.

$encargo = @'
Trabajas solo, sin nadie mirando. Lee primero CONTEXTO.md: es el mapa del
proyecto y trae las reglas de trabajo y las trampas conocidas del repositorio.

Coge UNA sola tarea de MEJORAS.md —la primera que no esté marcada como hecha—,
hazla entera y para. Una cosa por ejecución, no tres a medias.

Obligatorio antes de dar nada por terminado:
  1. Compilación limpia con -Werror.
  2. ctest entero en verde.
  3. Arrancar la aplicación (build/release/pc_inspector.exe --smoke).
Las tres. Si alguna falla, arréglalo o deshaz el cambio; no dejes el
repositorio roto.

Escribe una prueba para cualquier lógica nueva que no sea trivial, actualiza
README.md y ARQUITECTURA.md si el cambio lo pide, marca la tarea en MEJORAS.md,
y haz UN commit atómico. El mensaje de commit en español, sin firma de
atribución ni Co-Authored-By.

Si no puedes terminar la tarea, no la dejes a medias: deshaz lo tocado, anota
en MEJORAS.md que lo intentaste y por qué no salió, y commitea solo esa nota.

No hagas push.
'@

if ($Publicar) {
    $encargo = $encargo -replace 'No hagas push\.', 'Al terminar, haz push a main.'
}

# --- Adelante ---------------------------------------------------------------

Set-Content -Path $cerrojo -Value (Get-Date -Format 'o') -Encoding utf8
try {
    Anotar '--- arranca ---'
    Anotar ('presupuesto {0:N2} USD, publicar={1}' -f $PresupuestoUsd, $Publicar)

    Push-Location $raiz
    try {
        $salida = & $claude.Source -p $encargo `
            --permission-mode acceptEdits `
            --max-budget-usd $PresupuestoUsd `
            --output-format text 2>&1
        $codigo = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    $texto = ($salida | Out-String).Trim()
    if ($texto.Length -gt 4000) { $texto = $texto.Substring(0, 4000) + ' […recortado]' }
    Anotar ("salida (codigo {0}):`n{1}" -f $codigo, $texto)

    if ($codigo -ne 0) {
        # Lo normal cuando no hay cupo. No es un fallo del script: es que aún
        # no toca. Se anota y se sale en silencio para no llenar el registro de
        # alarmas que no lo son.
        Anotar 'No se pudo trabajar en esta pasada (lo más probable: sin cupo todavía). Se reintenta en una hora.'
    } else {
        Anotar '--- terminado ---'
    }
} finally {
    Remove-Item -Path $cerrojo -Force -ErrorAction SilentlyContinue
}
