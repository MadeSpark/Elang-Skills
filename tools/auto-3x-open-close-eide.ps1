<#
Fully automated "3x open + close eIDE and verify no hook freeze" cycle.
No user interaction required. We do all 3 cycles ourselves.

For each cycle:
  t0: capture baseline (WER newest + e.exe count + hook DLL loader count)
  t1: Start e.exe (loads iDraw + CeTools + DIY component automatically via lib\ plugins)
      wait 5s for iDraw to install WH_KEYBOARD_LL / WH_MOUSE_LL hooks
  t2: ask e.exe nicely to exit via WM_QUIT -> wait up to 10s, if still alive -> TerminateProcess cleanly
  t3: immediately after e.exe exit: check
        (i) e.exe processes still alive = 0?
        (ii) any process still loading CeTools/DIYcomponent/iDraw DLLs? (hook holder leak)
        (iii) WER newest timestamp unchanged?
  Every item PASS -> cycle OK.

Cycle x3 -> final report.
ASCII only, terminal smooth.
#>
$ErrorActionPreference = "Continue"

# --- Hard paths validated present ---
$EExe = "D:\ides\e\e.exe"
if(-not (Test-Path $EExe)){ Write-Host "[FATAL] e.exe not found at $EExe" ; exit 2 }

$ToolsDir = $PSScriptRoot
if(-not $ToolsDir){ $ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$Fix     = Join-Path $ToolsDir "fix-eide-kb-freeze.ps1"
$Verify  = Join-Path $ToolsDir "verify-eide-kb-freeze.ps1"

function L($s){ Write-Host ("[Auto3x {0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $s) }

# ========================= PRECHECK: Run fix script, ensure 4 guards up =========================
L "Precheck: Run fix-eide-kb-freeze first (defensive)"
& powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Fix
$exitFix = $LASTEXITCODE
L "Precheck fix exit=$exitFix"

# ========================= UTILS =========================
function Get-NewestWerTime {
  $paths = @("$env:ProgramData\Microsoft\Windows\WER\ReportArchive","$env:LOCALAPPDATA\CrashDumps")
  $t = [DateTime]::MinValue
  foreach($pa in $paths){
    if(Test-Path $pa){
      $items = Get-ChildItem $pa -Filter "*e.exe*" -ErrorAction 0
      if($items){ $m = ($items | Measure-Object LastWriteTime -Maximum).Maximum ; if($m -gt $t){ $t = $m } }
    }
  }
  return $t
}

function Count-ProcessesWithHookDlls {
  $targets = @('CeTools','DIYcomponent','iDraw')
  $count = 0
  foreach($p in (Get-Process -ErrorAction 0)){
    $hit = $false
    try {
      $loaded = $p.Modules
      if($loaded){
        foreach($m in $loaded){
          try {
            $mname = $m.ModuleName
            if(-not $mname){ continue }
            foreach($t in $targets){
              if($mname -like "*$t*"){ $hit = $true ; break }
            }
            if($hit){ break }
          } catch { continue }
        }
      }
    } catch { }
    if($hit){ $count++ }
  }
  return $count
}

function Count-EProcesses {
  $x = Get-Process -Name e -ErrorAction 0
  if($x){ return @($x).Count } else { return 0 }
}

function Start-ProcessAndGetPid($Exe, $Args = $null) {
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $Exe
  if($Args){ $psi.Arguments = $Args }
  $psi.UseShellExecute = $false
  $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
  $proc = New-Object System.Diagnostics.Process
  $proc.StartInfo = $psi
  [void]$proc.Start()
  Start-Sleep -Milliseconds 300
  try { return $proc.Id } catch { return -1 }
}

function Stop-ProcessCleanly([int]$pidToKill, [int]$maxWaitSec=10) {
  try { $p = Get-Process -Id $pidToKill -ErrorAction 0 } catch { $p = $null }
  if(-not $p){ return "AlreadyDead" }
  # ask nicely first: CloseMainWindow
  try {
    if($p.MainWindowHandle -ne [IntPtr]::Zero){ [void]$p.CloseMainWindow() }
    else { [void]$p.Close() }
  } catch {}
  $deadline = (Get-Date).AddSeconds($maxWaitSec)
  do {
    Start-Sleep -Milliseconds 250
    try { $alive = Get-Process -Id $pidToKill -ErrorAction 0 } catch { $alive = $null }
    if(-not $alive){ return "ExitedNicely" }
  } while((Get-Date) -lt $deadline)
  # did not exit nicely => TerminateProcess
  try { Stop-Process -Id $pidToKill -Force -ErrorAction 0 ; Start-Sleep -Milliseconds 500 } catch {}
  try { $alive2 = Get-Process -Id $pidToKill -ErrorAction 0 } catch { $alive2 = $null }
  if($alive2){ return "TerminateFailed" } else { return "Terminated" }
}

# ========================= GLOBAL BASELINE =========================
$baselineWer = Get-NewestWerTime
$baselineHookDll = Count-ProcessesWithHookDlls
$baselineE = Count-EProcesses
L ("GLOBAL BASELINE t0:  e.exe running={0}   DLL-holder-count={1}   newest-e.exe-wer={2}" -f $baselineE,$baselineHookDll,$baselineWer)
# Kill any pre-existing zombie e.exe (clean plate)
if($baselineE -gt 0){
  L ("Found {0} pre-existing e.exe. Killing them all cleanly for clean baseline." -f $baselineE)
  Get-Process -Name e -ErrorAction 0 | ForEach-Object { Stop-ProcessCleanly $_.Id 5 | Out-Null }
  Start-Sleep -Seconds 2
  $baselineE = Count-EProcesses
  L ("Post-clean e.exe running={0}" -f $baselineE)
}
if($baselineHookDll -gt 0){
  L ("WARN: {0} processes hold hook DLLs before we start (might be another app). Monitoring delta per cycle." -f $baselineHookDll)
}

# ========================= 3 CYCLES =========================
$failCount = 0
$cycleReport = @()

1..3 | ForEach-Object {
  $cycle = $_
  L ("============ CYCLE {0}/3 START ============" -f $cycle)
  $cyWerBefore = Get-NewestWerTime
  $cyHookBefore = Count-ProcessesWithHookDlls
  $cyEBefore = Count-EProcesses
  L ("{0}/3 t=before-start:  e={1}  hookDll={2}  wer={3}" -f $cycle,$cyEBefore,$cyHookBefore,$cyWerBefore)

  # ----- start e.exe -----
  $pidE = Start-ProcessAndGetPid $EExe $null
  if($pidE -lt 0){
    L ("FAIL  {0}/3: Could not start e.exe." -f $cycle)
    $failCount++
    $cycleReport += ("{0}/3: FAILED (e.exe not started)" -f $cycle)
    return
  }
  L ("{0}/3: e.exe started PID={1}. Waiting 5s for iDraw+CeTools to install hooks (WH_KEYBOARD_LL/WH_MOUSE_LL)." -f $cycle,$pidE)
  Start-Sleep -Seconds 5

  # sanity: e.exe still alive after 5s?
  $pAlive = Get-Process -Id $pidE -ErrorAction 0
  if(-not $pAlive){
    L ("WARN  {0}/3: e.exe PID={1} crashed/vanished during 5s wait. (means CeTools/DIY crashed -> FAILED THIS CYCLE)" -f $cycle,$pidE)
    $failCount++
    $cycleReport += ("{0}/3: FAILED (e.exe crashed before close)" -f $cycle)
    return
  }
  $cyHookAfterStart = Count-ProcessesWithHookDlls
  L ("{0}/3 t=post-load:     e alive={1}  hookDll={2}  (delta +{3})" -f $cycle,
     $(if($pAlive){"YES"}else{"NO"}), $cyHookAfterStart, ($cyHookAfterStart - $cyHookBefore))

  # ----- close e.exe -----
  L ("{0}/3: Now closing e.exe PID={1} (clean CloseMainWindow -> wait 10s -> TerminateProcess if stuck)" -f $cycle,$pidE)
  $howDied = Stop-ProcessCleanly $pidE 10
  Start-Sleep -Milliseconds 800  # allow FreeLibrary calls to flush

  # ----- immediate post-close checks -----
  $cyEAfterClose = Count-EProcesses
  $cyHookAfterClose = Count-ProcessesWithHookDlls
  $cyWerAfterClose = Get-NewestWerTime

  $deltaHook = ($cyHookAfterClose - $cyHookBefore)  # ideally 0
  $werChanged = ($cyWerAfterClose -ne $cyWerBefore)
  $zeroEAfter  = ($cyEAfterClose -eq 0)
  $noHookLeak  = ($deltaHook -le 0)
  $noNewCrash  = (-not $werChanged)

  L ("{0}/3 t=post-close:    exit-method={1}   e-exists={2}/{3}   hookDll-before-after={4}->{5} (delta={6})   wer-changed={7} newest={8}" -f $cycle,
      $howDied, $cyEAfterClose, $(if($zeroEAfter){"OK"}else{"FAIL"}),
      $cyHookBefore,$cyHookAfterClose,$deltaHook, $(if($werChanged){"FAIL-NEW-CRASH"}else{"OK"}), $cyWerAfterClose)

  $cyPass = $true
  if(-not $zeroEAfter){ L ("FAIL {0}/3: e.exe still alive after close -> zombie/mutex-stuck" -f $cycle) ; $failCount++ ; $cyPass = $false }
  if(-not $noHookLeak){ L ("FAIL {0}/3: hook DLL holder count delta +{1} -> iDraw/CeTools hooks leaked (the freeze root cause)" -f $cycle,$deltaHook) ; $failCount++ ; $cyPass = $false }
  if(-not $noNewCrash){ L ("FAIL {0}/3: WER newest changed from {1} -> {2}!  CeTools/DIY crashed AGAIN (close-stage stack buffer overrun)." -f $cycle,$cyWerBefore,$cyWerAfterClose) ; $failCount++ ; $cyPass = $false }
  if($cyPass){ L ("PASS  {0}/3: (zero zombie) AND (no hook DLL leak) AND (no new WER crash). -> hooks released, mouse/keyboard NOT frozen" -f $cycle) }
  $cycleReport += ("{0}/3: {1}  howClose={2}  eAfter={3}  hookDelta={4}  werNew={5}" -f $cycle,
    $(if($cyPass){"PASS"}else{"FAIL"}), $howDied, $cyEAfterClose, $deltaHook, $cyWerAfterClose)
}

# ========================= FINAL =========================
Write-Host ""
L "================= FINAL RUN verify-eide-kb-freeze + snapshot ========================="
& powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Verify
$exitVer = $LASTEXITCODE
L ("Final verify exit={0}" -f $exitVer)

Write-Host ""
L "=================================== REPORT ======================================="
Write-Host ""
Write-Host ("Global baseline newest WER (t0)        : {0}" -f $baselineWer)
Write-Host ("Global end     newest WER (tFinal)     : {0}" -f (Get-NewestWerTime))
Write-Host ("WER changed between t0 and end? (must be NO)  : {0}" -f ("$((Get-NewestWerTime) -ne $baselineWer)".ToUpper()))
Write-Host ""
Write-Host "Per-cycle report:"
foreach($line in $cycleReport){ Write-Host ("  {0}" -f $line) }
Write-Host ""
Write-Host ("TOTAL FAIL count across 3 cycles  : {0}" -f $failCount)
Write-Host ("TOTAL exit verify (9 guards)      : {0}" -f $exitVer)
Write-Host ""
if(($failCount -eq 0) -and ($exitVer -eq 0)){
  Write-Host "[AUTOMATIC 3x CYCLES VERIFICATION: ALL PASS. iDraw hooks released correctly after every close. Mouse/keyboard NOT frozen."
  exit 0
} else {
  Write-Host "[AUTOMATIC 3x CYCLES VERIFICATION: FAIL. See FAIL lines above for the leaking step.]"
  exit 1
}
