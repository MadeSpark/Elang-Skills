param([switch]$FocusedWin32k)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$capture = Join-Path $root ".dbg"
if (!(Test-Path -LiteralPath $capture)) { throw "Diagnostic directory missing" }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$kind = if ($FocusedWin32k) { "input-win32k-" } else { "input-cpu-" }
$trace = Join-Path $capture ($kind + $stamp + ".etl")
$log = Join-Path $capture ($kind + $stamp + ".driver.log")
$profile = if ($FocusedWin32k) { '"' + (Join-Path $PSScriptRoot "HookLatency\InputWin32k.wprp") + '!InputWin32k"' } else { "CPU" }
$wpr = Join-Path $env:windir "System32\wpr.exe"
$shell = Join-Path $env:windir "System32\WindowsPowerShell\v1.0\powershell.exe"
$driver = Join-Path $PSScriptRoot "run-mouse-rate-sampler-3cycles.ps1"
$started = $false
try {
    # Only tracing control is elevated; the IDE and input probe stay at user integrity.
    $start = Start-Process $wpr -ArgumentList @("-start",$profile,"-filemode") -Verb RunAs -PassThru -Wait
    if ($start.ExitCode -ne 0) { throw "WPR start failed; existing sessions will not be cancelled" }
    $started = $true
    $test = Start-Process $shell -ArgumentList @("-NoProfile","-File",('"{0}"' -f $driver),"-Mode","Auto","-Cycles","1") `
        -RedirectStandardOutput $log -RedirectStandardError ($log + ".stderr") -PassThru -Wait
    Write-Host ("DIAGNOSTIC_EXIT=" + $test.ExitCode)
} finally {
    if ($started) {
        $stop = Start-Process $wpr -ArgumentList @("-stop",('"{0}"' -f $trace)) -Verb RunAs -PassThru -Wait
        if ($stop.ExitCode -ne 0) {
            throw "WPR save failed; inspect the recording session before any further trace"
        }
        Write-Host ("TRACE=" + $trace)
        Write-Host ("DRIVER_LOG=" + $log)
    }
}
