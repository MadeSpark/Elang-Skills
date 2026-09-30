<#
Master script: fully automated 3x (open eIDE + close) WITH HookChainInspector running in parallel.
Inspector continuously measures:
  - WH_MOUSE_LL / WH_KEYBOARD_LL CallNextHookEx return DURATION per event
  - every 500ms which processes load CeTools/DIY/iDraw DLLs
=> Any > 500ms duration EXACTLY while closing eIDE = smoking gun (the exact hook callback that freezes).
We also correlate with DLL snapshot: when e.exe already dead but some hook still returns > 2000ms => some unloaded DLL's hookproc still hanging (wild hook).
#>
$ErrorActionPreference = "Continue"
$ToolsDir = $PSScriptRoot
if(-not $ToolsDir){ $ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$HLDir = Join-Path $ToolsDir "HookLatency"
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$InspSrc = Join-Path $HLDir "HookChainInspector.cs"
$InspExe = Join-Path $HLDir "HookChainInspector.exe"
$EExe = "D:\ides\e\e.exe"
$FixScr = Join-Path $ToolsDir "fix-eide-kb-freeze.ps1"

function L($s){ Write-Host ("[InspectorRun {0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $s) }

# --- compile inspector if newer ---
if(-not (Test-Path $InspExe) -or ((Get-Item $InspSrc -ErrorAction 0).LastWriteTime -gt (Get-Item $InspExe -ErrorAction 0).LastWriteTime)){
  L "Compiling HookChainInspector.cs -> HookChainInspector.exe"
  Push-Location $HLDir
  & $csc /nologo /out:HookChainInspector.exe HookChainInspector.cs
  Pop-Location
  if($LASTEXITCODE -ne 0){ L "csc FAIL exit=$LASTEXITCODE" ; exit 2 }
  L ("Compiled OK. Size={0}B" -f (Get-Item $InspExe).Length)
}

# --- preflight fix ---
L "Pre-flight fix"
& powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $FixScr
L ("fix exit=$LASTEXITCODE")

# --- cleanup leftover ---
Get-Process -Name e -ErrorAction 0 | Stop-Process -Force -ErrorAction 0
Start-Sleep -Milliseconds 1500

$failCycle = 0
1..3 | ForEach-Object {
  $cycle = $_
  $log = Join-Path $HLDir ("inspector_cycle{0}_{1}.log" -f $cycle,(Get-Date -Format "HHmmss"))
  L ("=========== INSPECTOR CYCLE {0}/3 START -> log={1} ===========" -f $cycle,$log)

  # Start inspector (baseline=3s, test=4s, post=15s => 22s total)
  $insp = Start-Process -FilePath $InspExe -WorkingDirectory $HLDir `
          -ArgumentList "3","4","15" `
          -RedirectStandardOutput $log -RedirectStandardError ($log+".err.log") `
          -NoNewWindow -PassThru

  # Baseline 3s (inspector records baseline LL hook chain)
  Start-Sleep -Seconds 3

  # TESTWINDOW starts at t=3s: open e.exe, wait 2s (hooks install), then CLOSE cleanly
  L ("{0}/3: OPEN e.exe" -f $cycle)
  $p = New-Object System.Diagnostics.Process
  $p.StartInfo = (New-Object System.Diagnostics.ProcessStartInfo $EExe)
  $p.StartInfo.UseShellExecute = $false
  [void]$p.Start()
  Start-Sleep -Milliseconds 200
  try { $pidE = $p.Id } catch { $pidE = -1 }
  L ("{0}/3: e.exe PID={1} started. Wait 2s for CeTools/iDraw/DIY hooks install." -f $cycle,$pidE)
  Start-Sleep -Seconds 2

  L ("{0}/3: CLOSE e.exe PID={1} (CloseMainWindow)" -f $cycle,$pidE)
  try { [void]$p.CloseMainWindow() } catch {}
  $deadline = (Get-Date).AddSeconds(10)
  do {
    Start-Sleep -Milliseconds 200
    try { $alive = Get-Process -Id $pidE -ErrorAction 0 } catch { $alive = $null }
    if(-not $alive){ break }
  } while((Get-Date) -lt $deadline)
  if($alive){
    L ("{0}/3: e.exe PID={1} still alive after 10s => TerminateProcess" -f $cycle,$pidE)
    Stop-Process -Id $pidE -Force -ErrorAction 0 ; Start-Sleep -Milliseconds 500
  } else {
    L ("{0}/3: e.exe PID={1} exited cleanly." -f $cycle,$pidE)
  }

  # inspector now in POST phase (~15s). Wait for it to finish naturally (timeout 30s).
  $waited = 0
  do {
    Start-Sleep -Milliseconds 500
    $waited += 500
    try { $done = Get-Process -Id $insp.Id -ErrorAction 0 } catch { $done = $null }
  } while($done -and $waited -lt 30000)
  try { if($done){ Stop-Process -Id $insp.Id -Force -ErrorAction 0 ; Start-Sleep -Milliseconds 300 } } catch {}

  # parse log
  $slowLines = Select-String -Path $log -Pattern "SUMMARY_SLOW|JANK|FREEZE|INSPECTOR EXIT|SLOW" -ErrorAction 0
  Write-Host ("`n{0}/3 Inspector log excerpts:" -f $cycle)
  if($slowLines){ $slowLines | ForEach-Object { Write-Host ("  {0}" -f $_.Line) } }
  $dllSnaps = Select-String -Path $log -Pattern "DLL (post |end |t\+)" -ErrorAction 0
  if($dllSnaps){ $dllSnaps | Select-Object -First 12 | ForEach-Object { Write-Host ("  {0}" -f $_.Line) } }
  $exitLine = Select-String -Path $log -Pattern "INSPECTOR EXIT:" -ErrorAction 0
  if($exitLine -match "FAIL"){ $failCycle++ ; L ("{0}/3 FAILED (measured freeze -> see log)" -f $cycle) }
  else { L ("{0}/3 PASS (no >2s freeze)" -f $cycle) }
}

L ("Total fail cycles = {0}/3. Logs in tools\HookLatency\inspector_cycle*.log" -f $failCycle)
if($failCycle -gt 0){ L "SMOKING GUN CAUGHT: see FREEZE lines and correlate with DLL snapshot when e.exe died but hook still stuck. -> identify exact module and fix its Unhook call." ; exit 1 }
else { L "All 3 PASS. No >2s freeze in CallNextHookEx chain measured." ; exit 0 }
