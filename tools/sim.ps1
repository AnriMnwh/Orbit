# Builds the desktop simulator, renders every screen and writes PNGs.
#   .\tools\sim.ps1                 all screens  -> sim\out\*.png
#   .\tools\sim.ps1 face            only screens whose name contains "face"
#
# Needs MSYS2's UCRT64 gcc, cmake and ninja (C:\msys64\ucrt64\bin).

param([string]$Filter = '')

$ErrorActionPreference = 'Continue'
$project = Split-Path -Parent $PSScriptRoot
$build   = Join-Path $env:USERPROFILE '.orbit-build-sim'
$shots   = Join-Path $build 'shots'
$data    = Join-Path $build 'data'
$out     = Join-Path $project 'sim\out'
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

python "$project\tools\gen_icons.py" | Out-Null
python "$project\tools\gen_fonts.py" | Out-Null
python "$project\tools\gen_logo.py" | Out-Null

if (-not (Test-Path "$build\build.ninja")) {
    cmake -S "$project\sim" -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc | Out-Null
}
# Re-glob so new source files are picked up.
cmake -S "$project\sim" -B $build | Out-Null
cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not (Test-Path "$data\photo_0.rgb565")) { python "$project\tools\sim_shots.py" data $data }
Remove-Item -Recurse -Force $shots, $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $shots | Out-Null

& "$build\orbit_sim.exe" $data $shots $Filter
if ($LASTEXITCODE -ne 0) { Write-Output "simulator exited with $LASTEXITCODE"; exit $LASTEXITCODE }
python "$project\tools\sim_shots.py" sheet $shots $out
