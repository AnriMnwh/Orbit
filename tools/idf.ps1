# Runs idf.py with the ESP-IDF 5.5.x environment loaded.
#   .\tools\idf.ps1 build
#   .\tools\idf.ps1 -p COM5 flash monitor
#
# The build directory lives outside OneDrive on purpose: OneDrive locks files
# mid-build and that breaks ninja.

# Not 'Stop': export.ps1 writes progress to stderr, which PowerShell 5.1 would
# otherwise treat as a terminating error.
$ErrorActionPreference = 'Continue'

$idfCandidates = @(
    $env:IDF_PATH,
    (Get-ChildItem 'C:\Espressif\frameworks\esp-idf-v5.5*' -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName),
    (Get-ChildItem 'C:\Espressif\esp-idf-v5.5*' -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName)
) | Where-Object { $_ -and (Test-Path (Join-Path $_ 'export.ps1')) }

$idfPath = $idfCandidates | Select-Object -First 1
if (-not $idfPath) {
    Write-Error "ESP-IDF 5.5.x not found under C:\Espressif. Install ESP-IDF 5.5.x or set IDF_PATH."
    exit 1
}

$venvCandidates = @(
    (Get-ChildItem 'C:\Espressif\python_env\idf5.5*' -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName),
    (Get-ChildItem (Join-Path $env:USERPROFILE '.espressif\python_env\idf5.5*') -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName)
) | Where-Object { $_ -and (Test-Path $_) }

$venv    = $venvCandidates | Select-Object -First 1
$build   = Join-Path $env:USERPROFILE '.orbit-build'
$project = Split-Path -Parent $PSScriptRoot

if (-not $env:ORBIT_IDF_READY) {
    $env:IDF_PATH = $idfPath
    if ($venv) {
        $env:IDF_PYTHON_ENV_PATH = $venv
        $env:PATH = "$venv\Scripts;$env:PATH"
    }
    . "$idfPath\export.ps1" | Out-Null
    $env:ORBIT_IDF_READY = '1'
}

# Icons and clock numerals are generated C files; regenerate them when a Python
# with Pillow is available (otherwise use the already-generated files in main/assets/gen).
$sysPy = (Get-Command py -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
if (-not $sysPy) {
    $sysPy = Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if ($sysPy) {
    $pyArgs = if ($sysPy -like '*\py.exe') { @('-3') } else { @() }
    & $sysPy @pyArgs -c "import PIL" 2>$null
    if ($LASTEXITCODE -eq 0) {
        & $sysPy @pyArgs "$project\tools\gen_icons.py" | Out-Null
        & $sysPy @pyArgs "$project\tools\gen_fonts.py" | Out-Null
        & $sysPy @pyArgs "$project\tools\gen_logo.py" | Out-Null
    }
}

& idf.py -C $project -B $build @args
exit $LASTEXITCODE
