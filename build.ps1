<#
    Builds Go Sequencer (VST3 + standalone) with MSVC.

        .\build.ps1                 # configure + build Release, run the rules tests
        .\build.ps1 -Install        # also copy the .vst3 into a VST3 folder
        .\build.ps1 -Config Debug
        .\build.ps1 -Clean          # wipe the build folder first
#>
param(
    [string] $Config = "Release",
    [switch] $Install,
    [switch] $Clean
)

$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"

if ($Clean -and (Test-Path $build)) {
    Write-Host "removing $build"
    Remove-Item -Recurse -Force $build
}

Write-Host "== configure ==" -ForegroundColor Cyan
cmake -S "$root" -B "$build" -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "== build ($Config) ==" -ForegroundColor Cyan
cmake --build "$build" --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "== rules tests ==" -ForegroundColor Cyan
$tests = Join-Path $build "$Config\GoRulesTests.exe"
if (Test-Path $tests) {
    & $tests | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { throw "Go rules tests failed" }
} else {
    Write-Warning "test binary not found at $tests"
}

$artefacts = Join-Path $build "GoSequencer_artefacts\$Config"
$vst3      = Join-Path $artefacts "VST3\Go Sequencer.vst3"
$standalone = Join-Path $artefacts "Standalone\Go Sequencer.exe"

Write-Host ""
Write-Host "VST3:       $vst3"
Write-Host "Standalone: $standalone"

if ($Install) {
    $targets = @(
        "$env:CommonProgramFiles\VST3",
        "$env:LOCALAPPDATA\Programs\Common\VST3"
    )

    $installed = $false

    foreach ($dir in $targets) {
        try {
            if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
            Copy-Item -Recurse -Force -Path $vst3 -Destination $dir
            Write-Host "installed to $dir" -ForegroundColor Green
            $installed = $true
            break
        } catch {
            Write-Warning "could not write to ${dir}: $($_.Exception.Message)"
        }
    }

    if (-not $installed) {
        Write-Warning "install failed - copy '$vst3' into a VST3 folder by hand, or run this script from an elevated prompt"
    }
}
