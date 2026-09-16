$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

$Runtime = Join-Path $Root "build\Release\VoltaDLSSRuntime.exe"
$UCR = Join-Path $Root "third_party\UCR\UCR.exe"

function Write-Header {
    Clear-Host

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "                     VoltaDLSS" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
}

function Get-TargetWindows {
    $excluded = @(
        "ApplicationFrameHost",
        "conhost",
        "powershell",
        "pwsh",
        "WindowsTerminal",
        "VoltaDLSSRuntime"
    )

    Get-Process |
        Where-Object {
            $_.MainWindowHandle -ne 0 -and
            $_.MainWindowTitle -and
            $_.ProcessName -notin $excluded
        } |
        Sort-Object MainWindowTitle |
        Select-Object Id, ProcessName, MainWindowTitle
}

function Select-TargetWindow {
    while ($true) {

        Write-Header

        Write-Host "TARGET WINDOW" -ForegroundColor Yellow
        Write-Host ""

        $windows = @(Get-TargetWindows)

        if ($windows.Count -eq 0) {
            Write-Host "No visible application windows found." -ForegroundColor Red
            Write-Host ""
            Read-Host "Press Enter to retry"
            continue
        }

        for ($i = 0; $i -lt $windows.Count; $i++) {
            $number = $i + 1

            Write-Host ("[{0}] {1}" -f $number, $windows[$i].MainWindowTitle)
            Write-Host ("    Process: {0}    PID: {1}" -f `
                $windows[$i].ProcessName,
                $windows[$i].Id)
        }

        Write-Host ""
        $choice = Read-Host "Select target window"

        $index = 0

        if ([int]::TryParse($choice, [ref]$index)) {

            if ($index -ge 1 -and $index -le $windows.Count) {
                return $windows[$index - 1]
            }
        }

        Write-Host ""
        Write-Host "Invalid selection." -ForegroundColor Red
        Start-Sleep -Milliseconds 800
    }
}

function Select-Quality {
    while ($true) {

        Write-Header

        Write-Host "QUALITY PRESET" -ForegroundColor Yellow
        Write-Host ""

        Write-Host "[1] Ultra Performance"
        Write-Host "    33.3% render resolution"
        Write-Host ""

        Write-Host "[2] Performance"
        Write-Host "    44.4% render resolution"
        Write-Host ""

        Write-Host "[3] Balanced"
        Write-Host "    50.0% render resolution"
        Write-Host ""

        Write-Host "[4] Quality"
        Write-Host "    66.7% render resolution"
        Write-Host ""

        Write-Host "[5] Native"
        Write-Host "    100% native resolution"
        Write-Host ""

        $choice = Read-Host "Select quality"

        switch ($choice) {
            "1" { return "ultra-performance" }
            "2" { return "performance" }
            "3" { return "balanced" }
            "4" { return "quality" }
            "5" { return "native" }
        }

        Write-Host ""
        Write-Host "Invalid selection." -ForegroundColor Red
        Start-Sleep -Milliseconds 800
    }
}

function Select-Remapper {
    while ($true) {

        Write-Header

        Write-Host "KEYBOARD & MOUSE REMAPPER" -ForegroundColor Yellow
        Write-Host ""

        Write-Host "Enable Keyboard & Mouse Remapper?"
        Write-Host ""
        Write-Host "[Y] Yes - launch bundled UCR"
        Write-Host "[N] No  - skip UCR"
        Write-Host ""

        $choice = Read-Host "Select"

        switch ($choice.ToUpper()) {
            "Y" { return $true }
            "N" { return $false }
        }

        Write-Host ""
        Write-Host "Please enter Y or N." -ForegroundColor Red
        Start-Sleep -Milliseconds 800
    }
}

function Start-UCR {
    if (-not (Test-Path $UCR)) {
        Write-Host ""
        Write-Host "UCR not found:" -ForegroundColor Red
        Write-Host $UCR
        Write-Host ""
        return $false
    }

    Write-Host ""
    Write-Host "Starting bundled UCR..." -ForegroundColor Cyan

    Start-Process `
        -FilePath $UCR `
        -WorkingDirectory (Split-Path $UCR)

    Start-Sleep -Milliseconds 500

    return $true
}

# ---------------------------------------------------------------------------
# Validate runtime
# ---------------------------------------------------------------------------

if (-not (Test-Path $Runtime)) {
    Write-Header

    Write-Host "VoltaDLSSRuntime.exe was not found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Expected:"
    Write-Host $Runtime
    Write-Host ""

    Read-Host "Press Enter to exit"
    exit 1
}

# ---------------------------------------------------------------------------
# Menu
# ---------------------------------------------------------------------------

$Target = Select-TargetWindow

$Quality = Select-Quality

$EnableUCR = Select-Remapper

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

$env:VOLTADLSS_TARGET = $Target.Id.ToString()
$env:VOLTADLSS_QUALITY = $Quality

if ($EnableUCR) {
    $env:VOLTADLSS_UCR = "1"
}
else {
    $env:VOLTADLSS_UCR = "0"
}

# ---------------------------------------------------------------------------
# Launch UCR if requested
# ---------------------------------------------------------------------------

if ($EnableUCR) {
    Start-UCR | Out-Null
}

# ---------------------------------------------------------------------------
# Final launch information
# ---------------------------------------------------------------------------

Write-Header

Write-Host "VoltaDLSS configuration" -ForegroundColor Cyan
Write-Host ""
Write-Host ("Target : {0}" -f $Target.MainWindowTitle)
Write-Host ("PID    : {0}" -f $Target.Id)
Write-Host ("Quality: {0}" -f $Quality)
Write-Host ("UCR    : {0}" -f $(if ($EnableUCR) { "Enabled" } else { "Disabled" }))
Write-Host ""

Write-Host "Starting VoltaDLSS..." -ForegroundColor Green
Write-Host ""

# ---------------------------------------------------------------------------
# Runtime
# ---------------------------------------------------------------------------

& $Runtime

$exitCode = $LASTEXITCODE

Write-Host ""
Write-Host "VoltaDLSS exited with code $exitCode." -ForegroundColor Yellow
Write-Host ""

Read-Host "Press Enter to close"

exit $exitCode
