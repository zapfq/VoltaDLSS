$ErrorActionPreference = "Stop"

$Project = "C:\Users\Rambu Tan\Desktop\VoltaDLSS"
$Build = "$Project\build"
$BuildRelease = "$Build\Release"
$Release = "$Project\release"

$CMake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

Write-Host ""
Write-Host "==============================================" -ForegroundColor Yellow
Write-Host "           VOLTADLSS BUILD + PACKAGE"
Write-Host "==============================================" -ForegroundColor Yellow
Write-Host ""

# ------------------------------------------------------------
# Configure
# ------------------------------------------------------------

Write-Host "[1/5] Configuring..." -ForegroundColor Cyan

& $CMake `
    -S $Project `
    -B $Build `
    -G "Visual Studio 17 2022" `
    -A x64 `
    "-DCUDAToolkit_ROOT=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9" `
    "-DCMAKE_CUDA_ARCHITECTURES=70"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

# ------------------------------------------------------------
# Build runtime
# ------------------------------------------------------------

Write-Host ""
Write-Host "[2/5] Building runtime..." -ForegroundColor Cyan

& $CMake `
    --build $Build `
    --config Release `
    --target VoltaDLSSRuntime `
    --parallel

if ($LASTEXITCODE -ne 0) {
    throw "VoltaDLSSRuntime build failed."
}

# ------------------------------------------------------------
# Build launcher
# ------------------------------------------------------------

Write-Host ""
Write-Host "[3/5] Building launcher..." -ForegroundColor Cyan

& $CMake `
    --build $Build `
    --config Release `
    --target VoltaDLSSLauncher `
    --parallel

if ($LASTEXITCODE -ne 0) {
    throw "VoltaDLSSLauncher build failed."
}

# ------------------------------------------------------------
# Verify binaries
# ------------------------------------------------------------

$LauncherExe = "$BuildRelease\VoltaDLSS.exe"
$RuntimeExe = "$BuildRelease\VoltaDLSSRuntime.exe"

if (-not (Test-Path $LauncherExe)) {
    throw "Launcher EXE was not produced: $LauncherExe"
}

if (-not (Test-Path $RuntimeExe)) {
    throw "Runtime EXE was not produced: $RuntimeExe"
}

# ------------------------------------------------------------
# Recreate release package
# ------------------------------------------------------------

Write-Host ""
Write-Host "[4/5] Packaging..." -ForegroundColor Cyan

if (Test-Path $Release) {
    Remove-Item `
        $Release `
        -Recurse `
        -Force
}

New-Item `
    -ItemType Directory `
    -Path $Release `
    -Force |
    Out-Null

New-Item `
    -ItemType Directory `
    -Path "$Release\bin" `
    -Force |
    Out-Null

New-Item `
    -ItemType Directory `
    -Path "$Release\config" `
    -Force |
    Out-Null

New-Item `
    -ItemType Directory `
    -Path "$Release\third_party" `
    -Force |
    Out-Null

# Launcher
Copy-Item `
    $LauncherExe `
    "$Release\VoltaDLSS.exe" `
    -Force

# Runtime
Copy-Item `
    $RuntimeExe `
    "$Release\bin\VoltaDLSSRuntime.exe" `
    -Force

# Configuration
$ConfigLines = @(
    "[VoltaDLSS]"
    "DefaultQuality=quality"
    "DefaultRemapper=0"
)

$ConfigLines |
    Set-Content `
        "$Release\config\config.ini" `
        -Encoding UTF8

# UCR
$UcrSource = "$Project\third_party\UCR"
$UcrDest = "$Release\third_party\UCR"

if (Test-Path $UcrSource) {

    New-Item `
        -ItemType Directory `
        -Path $UcrDest `
        -Force |
        Out-Null

    & robocopy `
        $UcrSource `
        $UcrDest `
        /E `
        /XD `
            "$UcrSource\Cache" `
            "$UcrSource\logs" `
            "$UcrSource\Interception\samples" `
        /NFL `
        /NDL `
        /NJH `
        /NJS `
        /NP

    if ($LASTEXITCODE -gt 7) {
        throw "UCR packaging failed. Robocopy exit code: $LASTEXITCODE"
    }
}

# Release README
$ReadmeLines = @(
    "VoltaDLSS"
    "========="
    ""
    "Volta Tensor Upscaler"
    ""
    "Target hardware:"
    "NVIDIA Titan V / Volta SM 7.0"
    ""
    "Launch:"
    "VoltaDLSS.exe"
    ""
    "The launcher handles:"
    "- Window selection"
    "- Quality selection"
    "- Keyboard + mouse remapping"
    ""
    "The runtime is located in:"
    "bin\VoltaDLSSRuntime.exe"
)

$ReadmeLines |
    Set-Content `
        "$Release\README.txt" `
        -Encoding UTF8

# ------------------------------------------------------------
# Create local shortcut
# ------------------------------------------------------------

Write-Host ""
Write-Host "[5/5] Creating root shortcut..." -ForegroundColor Cyan

$ShortcutPath = "$Project\VoltaDLSS.lnk"

$Shell =
    New-Object -ComObject WScript.Shell

$Shortcut =
    $Shell.CreateShortcut(
        $ShortcutPath)

$Shortcut.TargetPath =
    "$Release\VoltaDLSS.exe"

$Shortcut.WorkingDirectory =
    $Release

$Shortcut.Description =
    "VoltaDLSS - Volta Tensor Upscaler"

$Shortcut.Save()

# ------------------------------------------------------------
# Done
# ------------------------------------------------------------

Write-Host ""
Write-Host "==============================================" -ForegroundColor Green
Write-Host "              BUILD COMPLETE"
Write-Host "==============================================" -ForegroundColor Green
Write-Host ""

Write-Host "Release package:" -ForegroundColor White
Write-Host "$Release" -ForegroundColor Yellow
Write-Host ""

Get-ChildItem `
    $Release `
    -Recurse `
    -File |
    Select-Object `
        FullName,
        Length |
    Format-Table -AutoSize
