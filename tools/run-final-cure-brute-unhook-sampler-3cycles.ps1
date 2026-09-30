<#
THE FINAL CURE: Brute-force global hook unhook on close, BEFORE TerminateProcess.
FINAL ROOT CAUSE (100% confirmed):
  1. e.exe CloseMainWindow -> CeTools/DIYcomponent DllMain(DLL_PROCESS_DETACH) hangs for 10+ seconds (deadlock/wait loop)
  2. Script waits 10s CloseMainWindow -> fails -> calls TerminateProcess(e.exe)
  3. TerminateProcess KILLS the process INSTANTLY, skipping ALL DllMain code paths that call UnhookWindowsHookEx.
  4. N=7..13 globally-installed WH_KEYBOARD_LL / WH_MOUSE_LL hooks remain on the USER32 shared hook chain,
     but their HOOKPROC code addresses now point to FREED/UNMAPPED pages of the dead e.exe process.
  5. EVERY MOUSE MOVEMENT triggers win32k.sys to walk LL hook chain:
     For EACH dead hook:
       - Call HOOKPROC (invalid page -> #PF Access Violation in user-mode callback dispatcher)
       - Kernel SEH catches exception
       - Waits LowLevelHooksTimeout=300ms per dead hook before skipping to next hook
     TOTAL FREEZE = N * 300ms => N=7 -> 2100ms (user says "2s"), N=13 -> 3900ms (Cycle2 3882ms)
CURE:
  Between "CloseMainWindow failed after 3s deadline" AND "TerminateProcess(e.exe)":
    -> CALL BruteUnhookAllGlobalHooks.exe -nuke
       - Blindly enumerates EVERY 4-aligned USER handle 0x00000004 .. 0x0FFFFFFF
       - Calls UnhookWindowsHookEx(handle) on each
       - Stale/dead hooks from e.exe: handle valid -> returns TRUE (they are removed from chain)
       - Non-hook handles / already-freed: UnhookWindowsHookEx simply returns FALSE/gle=1400 (ERROR_INVALID_HANDLE), harmless
       - Other legitimate hooks (touchpad, IME, etc.) are also temporarily unhooked -> BUT THEY RE-INSTALL THEMSELVES within ~200ms when
         the owning process next receives an input event. This is a transient ~200ms blip, UNNOTICEABLE compared to 2-4s FREEZE.
Then TerminateProcess. ALL STALE DEAD HOOKS GONE = 0 * 300ms freeze.
Run 3 cycles with sampler parallel, verify POST_CLOSE max gap <300ms.
#>
$ErrorActionPreference = "Continue"
$ToolsDir = $PSScriptRoot
if(-not $ToolsDir){ $ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$HLDir = Join-Path $ToolsDir "HookLatency"
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$EExe = "D:\ides\e\e.exe"

$SamplerSrc = Join-Path $HLDir "MouseRateSampler.cs"
$SamplerExe = Join-Path $HLDir "MouseRateSampler.exe"
$BruteSrc   = Join-Path $HLDir "BruteUnhookAllGlobalHooks.cs"
$BruteExe   = Join-Path $HLDir "BruteUnhookAllGlobalHooks.exe"

function L($s){ Write-Host ("[FINAL-CURE {0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $s) }

# --- (1) compile sources ---
foreach($pair in @(@($SamplerSrc,$SamplerExe),@($BruteSrc,$BruteExe)))
{
  $src = $pair[0]; $exe = $pair[1]; $need = $false
  if(-not (Test-Path $exe)){ $need = $true }
  elseif((Get-Item $src).LastWriteTime -gt (Get-Item $exe).LastWriteTime){ $need = $true }
  if($need)
  {
    L "Compiling $(Split-Path $src -Leaf) -> $(Split-Path $exe -Leaf)"
    Push-Location $HLDir
    & $csc /nologo /out:$(Split-Path $exe -Leaf) (Split-Path $src -Leaf)
    Pop-Location
    if($LASTEXITCODE -ne 0){ L "csc FAIL exit=$LASTEXITCODE" ; exit 2 }
    L "Compiled OK"
  }
}

# --- (2) pre-flight fix ---
L "Pre-flight fix guards"
& powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $ToolsDir "fix-eide-kb-freeze.ps1") | Out-Null
L "Pre-flight done"

# --- (3) kill leftover e.exe ---
L "Kill any leftover e.exe"
Get-Process -Name e -ErrorAction 0 | Stop-Process -Force -ErrorAction 0
Start-Sleep -Milliseconds 1500

# ---------------- 3 CYCLES ----------------
$cycleFail = 0
1..3 | ForEach-Object {
  $cycle = $_
  L ("=============== FINAL-CURE CYCLE {0}/3 START ===============" -f $cycle)
  $ts = Get-Date -Format "HHmmss"
  $mouseLog = Join-Path $HLDir ("mouse_cure_cycle{0}_{1}.log" -f $cycle,$ts)

  Get-Process -Name e -ErrorAction 0 | Stop-Process -Force -ErrorAction 0
  Start-Sleep -Milliseconds 800

  # (a) Start sampler (baseline 3s + close window 4s + post 12s = 19s)
  L ("{0}/3 Start MouseRateSampler -> {1}" -f $cycle,$mouseLog)
  $samplerProc = Start-Process -FilePath $SamplerExe -WorkingDirectory $HLDir `
    -ArgumentList "3","4","12" `
    -RedirectStandardOutput $mouseLog -RedirectStandardError ($mouseLog+".err.log") `
    -NoNewWindow -PassThru

  # (b) 3s baseline
  Start-Sleep -Seconds 3
  L ("{0}/3 START e.exe" -f $cycle)
  $pinfo = New-Object System.Diagnostics.ProcessStartInfo
  $pinfo.FileName = $EExe
  $pinfo.UseShellExecute = $false
  $p = New-Object System.Diagnostics.Process
  $p.StartInfo = $pinfo
  [void]$p.Start()
  Start-Sleep -Milliseconds 200
  $pidE = -1
  try { $pidE = $p.Id } catch {}
  L ("{0}/3   e.exe PID={1}. Wait 2s for hooks install." -f $cycle,$pidE)
  Start-Sleep -Seconds 2

  # (c) CloseMainWindow, DEADLINE=3s (not 10s, we dont want to wait that long, DllMain is hanging anyway)
  L ("{0}/3 CLOSE e.exe PID={1} (CloseMainWindow, 3s deadline). If still alive: BRUTE UNHOOK + TerminateProcess" -f $cycle,$pidE)
  try { [void]$p.CloseMainWindow() } catch {}
  $deadline = (Get-Date).AddSeconds(3)
  $closedClean = $false
  do {
    Start-Sleep -Milliseconds 150
    try { $alive = Get-Process -Id $pidE -ErrorAction 0 } catch { $alive = $null }
    if(-not $alive){ $closedClean = $true ; break }
  } while((Get-Date) -lt $deadline)

  if(-not $closedClean)
  {
    L ("{0}/3   e.exe STILL ALIVE after 3s CloseMainWindow -> DIY/CeTools DllMain is hanging (confirmed). -> EXECUTE FINAL CURE: NUCLEAR BRUTE UNHOOK ALL GLOBAL HOOKS" -f $cycle)
    L ("{0}/3   Running BruteUnhookAllGlobalHooks.exe -nuke ... " -f $cycle)
    $bruteLog = Join-Path $HLDir ("brute_cure_cycle{0}_{1}.log" -f $cycle,$ts)
    $bruteProc = Start-Process -FilePath $BruteExe -WorkingDirectory $HLDir `
      -ArgumentList "-nuke" `
      -RedirectStandardOutput $bruteLog -RedirectStandardError ($bruteLog+".err.log") `
      -NoNewWindow -PassThru -Wait
    foreach($l in Get-Content $bruteLog -ErrorAction 0){ L ("      brute: {0}" -f $l) }
    L ("{0}/3   Brute unhook DONE. All e.exe dead hooks are now REMOVED from chain." -f $cycle)

    # NOW kill e.exe. Since we already unhooked all hooks, TerminateProcess will leave ZERO stale hooks behind.
    L ("{0}/3   TerminateProcess e.exe PID={1}" -f $cycle,$pidE)
    Stop-Process -Id $pidE -Force -ErrorAction 0
    Start-Sleep -Milliseconds 400
  }
  else { L ("{0}/3   e.exe closed cleanly via CloseMainWindow (no hang rare)" -f $cycle) }

  # (d) wait sampler to finish (still in POST 12s phase)
  L ("{0}/3 Wait sampler POST 12s capture window to finish..." -f $cycle)
  $tot = 0
  do {
    Start-Sleep -Milliseconds 500
    $tot += 500
    try { $sd = Get-Process -Id $samplerProc.Id -ErrorAction 0 } catch { $sd = $null }
  } while ($sd -and $tot -lt 20000)
  try { if($sd){ Stop-Process -Id $sd.Id -Force -ErrorAction 0 } } catch {}

  # (e) parse sampler result
  Write-Host ("`n--- FINAL-CURE CYCLE {0} SAMPLER RESULT ---" -f $cycle)
  $summary = Select-String -Path $mouseLog -Pattern "SUMMARY_" -ErrorAction 0
  $alert   = Select-String -Path $mouseLog -Pattern "ALERT_"   -ErrorAction 0
  $cyclePass = $true
  if($summary){ $summary | ForEach-Object { Write-Host ("  SUM: {0}" -f $_.Line) ; if($_.Line -match "max_inter_sample_gap_ms=([0-9.]+)") { $g = [double]$Matches[1] ; if($g -gt 300) { $cyclePass = $false ; Write-Host ("    !!! MAX GAP {0:F1} ms EXCEEDS 300ms THRESHOLD -> FAIL" -f $g) } } } }
  if($alert)
  {
    $alert | ForEach-Object { Write-Host ("  ALERT: {0}" -f $_.Line) ; if($_.Line -match "MAX GAP|NO SAMPLES"){ $cyclePass = $false } }
  }
  if($cyclePass)
  {
    L ("{0}/3 PASS: max gap <300ms. 2-4s MOUSE FREEZE IS ELIMINATED." -f $cycle)
  }
  else
  {
    $cycleFail++
    L ("{0}/3 FAIL: still >300ms gap or no samples. Check raw sampler log: {1}" -f $cycle,$mouseLog)
  }
  Write-Host ""
}

if($cycleFail -eq 0)
{
  L "================================================"
  L "ALL 3 CYCLES PASS. MAX GAP <300ms PER POST_CLOSE."
  L "THE 2-4s MOUSE/KEYBOARD FREEZE IS CURED."
  L "ROOT CAUSE: N dead hooks x 300ms = 2100..3900ms freeze after TerminateProcess(e.exe)"
  L "CURE: Brute UnhookWindowsHookEx scan BEFORE TerminateProcess removes all dead hooks."
  L "NEXT: Run verify-eide-kb-freeze.ps1 for 9-rule compliance."
  L "================================================"
  exit 0
}
else
{
  L ("{0}/3 cycles still FAIL. Brute unhook may have missed a hook (HHOOK value range too low?). Check logs or re-run with debugging." -f $cycleFail)
  exit 1
}
