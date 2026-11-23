# Runs idf.py with the ESP-IDF 5.5.5 environment loaded.
#   .\tools\idf.ps1 build
#   .\tools\idf.ps1 -p COM5 flash monitor
#
# The build directory lives outside OneDrive on purpose: OneDrive locks files
# mid-build and that breaks ninja.

# Not 'Stop': export.ps1 writes progress to stderr, which PowerShell 5.1 would
# otherwise treat as a terminating error.
$ErrorActionPreference = 'Continue'

$idfPath = 'C:\Espressif\esp-idf-v5.5.5'
$venv    = Join-Path $env:USERPROFILE '.espressif\python_env\idf5.5_py3.11_env'
$build   = Join-Path $env:USERPROFILE '.orbit-build'
$project = Split-Path -Parent $PSScriptRoot

if (-not $env:ORBIT_IDF_READY) {
    $env:IDF_PATH = $idfPath
    $env:IDF_PYTHON_ENV_PATH = $venv
    $env:PATH = "$venv\Scripts;$env:PATH"
    . "$idfPath\export.ps1" | Out-Null
    $env:ORBIT_IDF_READY = '1'
}

# Icons and clock numerals are generated C files; keep them current. They need
# Pillow, which lives in the system Python, not in ESP-IDF's own environment,
# hence the "py" launcher rather than "python".
py -3 "$project\tools\gen_icons.py" | Out-Null
py -3 "$project\tools\gen_fonts.py" | Out-Null
py -3 "$project\tools\gen_logo.py" | Out-Null

& idf.py -C $project -B $build @args
exit $LASTEXITCODE
