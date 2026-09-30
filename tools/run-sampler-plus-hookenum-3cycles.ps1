<#
Parallel sampler + LL hook enumerator driver, 3 cycles.
Cycle N:
  T0s       - start EnumerateLLHooks.exe (background, log to hooks_cycleN_*.csv, -d 22s, -i 100ms)
  T0+0.5s   - start MouseRateSampler.exe (baseline 3s + closeWindow 4s + post 12s = 19s, log to mouse_cycleN_*.log)
  T0+3.5s   - (inside sampler TESTWINDOW) START e.exe
  T0+5.5s   - (hooks settled) CLOSE e.exe (CloseMainWindow, 10s deadline -> TerminateProcess)
  T0+22s    - both procs exit by now (EnumerateLLHooks always 22s total)
  - parse outputs:
      - mouse_cycleN ALERT / SUMMARY lines
      - hooks_cycleN.csv: lines where hook_type in {WH_MOUSE_LL, WH_KEYBOARD_LL} AND owner_pid refers to a dead e.exe (the PID just closed) AND module path in {DIYcomponent, CeTools, iDraw}
        -> if hookProc still present for dead pid between (close_t_us .. close_t_us + 4000000 us) -> STALE WILD HOOK LOCKED FOR X MS -> THAT IS THE 3.8S FREEZE
Run 3 cycles.
#>
$ErrorActionPreference = "Continue"
$ToolsDir = $PSScriptRoot
if(-not $ToolsDir){ $ToolsDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$HLDir = Join-Path $ToolsDir "HookLatency"
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$EExe = "D:\ides\e\e.exe"
$SamplerSrc = Join-Path $HLDir "MouseRateSampler.cs"
$SamplerExe = Join-Path $HLDir "MouseRateSampler.exe"
$EnumSrc    = Join-Path $HLDir "EnumerateLLHooks.cs"
$EnumExe    = Join-Path $HLDir "EnumerateLLHooks.exe"

function L($s){ Write-Host ("[EnumRun {0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"), $s) }

# --- (1) compile both exes if out-of-date ---
foreach($pair in @(@($SamplerSrc,$SamplerExe),@($EnumSrc,$EnumExe)))
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

# --- (3) kill leftover e.exe ---
L "Kill any leftover e.exe"
Get-Process -Name e -ErrorAction 0 | Stop-Process -Force -ErrorAction 0
Start-Sleep -Milliseconds 1500

# ---------------- 3 CYCLES ----------------
$staleHooksFound = 0
1..3 | ForEach-Object {
  $cycle = $_
  L ("=============== CYCLE {0}/3: sampler + enumerator parallel START ===============" -f $cycle)
  $ts = Get-Date -Format "HHmmss"

  $enumLog  = Join-Path $HLDir ("hooks_cycle{0}_{1}.csv" -f $cycle,$ts)
  $mouseLog = Join-Path $HLDir ("mouse_cycle{0}_{1}.log" -f $cycle,$ts)

  # cleanup leftover e.exe again
  Get-Process -Name e -ErrorAction 0 | Stop-Process -Force -ErrorAction 0
  Start-Sleep -Milliseconds 800

  # (a) start EnumerateLLHooks first (longer, background, captures all t0 baseline)
  L ("{0}/3 Start EnumerateLLHooks (22s total, 100ms snapshots) -> {1}" -f $cycle,$enumLog)
  $enumProc = Start-Process -FilePath $EnumExe -WorkingDirectory $HLDir `
    -ArgumentList "-d","22","-i","100","-o",$enumLog `
    -RedirectStandardOutput ($enumLog+".stdout.log") -RedirectStandardError ($enumLog+".err.log") `
    -NoNewWindow -PassThru

  # (b) 0.5s later: start MouseRateSampler
  Start-Sleep -Milliseconds 500
  L ("{0}/3 Start MouseRateSampler (baseline 3s + test 4s + post 12s = 19s) -> {1}" -f $cycle,$mouseLog)
  $samplerProc = Start-Process -FilePath $SamplerExe -WorkingDirectory $HLDir `
    -ArgumentList "3","4","12" `
    -RedirectStandardOutput $mouseLog -RedirectStandardError ($mouseLog+".err.log") `
    -NoNewWindow -PassThru

  # (c) baseline 3s (enumerator t=0.5..3.5s = sampler baseline 0..3s). Then start e.exe
  Start-Sleep -Seconds 3
  L ("{0}/3 START e.exe (inside sampler TESTWINDOW). Wait 2s for hooks to install." -f $cycle)
  $pinfo = New-Object System.Diagnostics.ProcessStartInfo
  $pinfo.FileName = $EExe
  $pinfo.UseShellExecute = $false
  $p = New-Object System.Diagnostics.Process
  $p.StartInfo = $pinfo
  [void]$p.Start()
  Start-Sleep -Milliseconds 200
  $pidE = -1
  try { $pidE = $p.Id } catch {}
  L ("{0}/3   e.exe PID={1}. Wait 2s for CeTools/iDraw hooks install." -f $cycle,$pidE)
  Start-Sleep -Seconds 2

  # (d) Close e.exe cleanly. Record exact close wall time (we'll convert to enumerator relative t_us later via CSV lines PROCESS_MARKER_e_EXE_ALIVE disappearance)
  L ("{0}/3 CLOSE e.exe PID={1} (CloseMainWindow, deadline=10s then TerminateProcess)" -f $cycle,$pidE)
  try { [void]$p.CloseMainWindow() } catch {}
  $deadline = (Get-Date).AddSeconds(10)
  $closeNeededTerminate = $false
  do {
    Start-Sleep -Milliseconds 200
    try { $alive = Get-Process -Id $pidE -ErrorAction 0 } catch { $alive = $null }
    if(-not $alive){ break }
  } while((Get-Date) -lt $deadline)
  if($alive)
  {
    L ("{0}/3   e.exe still alive after 10s CloseMainWindow -> TerminateProcess (still holding hooks -> THIS CONTRIBUTES TO FREEZE)" -f $cycle)
    Stop-Process -Id $pidE -Force -ErrorAction 0
    $closeNeededTerminate = $true
    Start-Sleep -Milliseconds 500
  }
  else { L ("{0}/3   e.exe exited cleanly via CloseMainWindow (good)" -f $cycle) }

  # (e) wait for both sampler and enumerator to finish (enumerator 22s total = started T0, ends ~T0+22; sampler started T0+0.5, ends ~T0+0.5+19=T0+19.5)
  L ("{0}/3 Wait for sampler + enumerator to finish (post-close capture window, ~10s more)" -f $cycle)
  $tot = 0
  do {
    Start-Sleep -Milliseconds 500
    $tot += 500
    try { $s1 = Get-Process -Id $samplerProc.Id -ErrorAction 0 } catch { $s1 = $null }
    try { $s2 = Get-Process -Id $enumProc.Id -ErrorAction 0 } catch { $s2 = $null }
  } while(($s1 -or $s2) -and $tot -lt 20000)
  try { if($s1){ Stop-Process -Id $s1.Id -Force -ErrorAction 0 } } catch {}
  try { if($s2){ Stop-Process -Id $s2.Id -Force -ErrorAction 0 } } catch {}

  # (f) quick parse
  Write-Host ("`n--- CYCLE {0} MOUSE SAMPLER SUMMARY ---" -f $cycle)
  $summary = Select-String -Path $mouseLog -Pattern "SUMMARY_" -ErrorAction 0
  $alert   = Select-String -Path $mouseLog -Pattern "ALERT_"   -ErrorAction 0
  if($summary){ $summary | ForEach-Object { Write-Host ("  SUM: {0}" -f $_.Line) } }
  if($alert){   $alert   | ForEach-Object { Write-Host ("  ALERT: {0}" -f $_.Line) } }

  Write-Host ("`n--- CYCLE {0} HOOK ENUMERATOR STALE-WILD CHECK (looking for dead pid=$pidE e.exe DIYcomponent/CeTools hook STILL ALIVE in post-close window) ---" -f $cycle)
  # Read csv lines, collect entries where WH_*_LL AND owner_pid == $pidE AND (mod contains DIYcomponent OR CeTools OR iDraw)
  $staleEntries = @()
  $maxTusStale = 0L
  $minTusAlive = [long]::MaxValue
  $maxTusAlive = 0L
  if(Test-Path $enumLog)
  {
    foreach($line in Get-Content $enumLog -ErrorAction 0)
    {
      if($line -match "^PROCESS_MARKER_e_EXE_ALIVE")
      {
        $parts = $line -split ','
        $tus = [long]$parts[0]
        if($tus -lt $minTusAlive){ $minTusAlive = $tus }
        if($tus -gt $maxTusAlive){ $maxTusAlive = $tus }
      }
      elseif($line -match "^[0-9]+,WH_(MOUSE|KEYBOARD)_LL,")
      {
        # simple parse: split on ',' but path might be quoted
        $quoteParts = @()
        $inQ = $false; $cur = ""
        for($i=0;$i -lt $line.Length;$i++){
          $c = $line[$i]
          if($c -eq '"'){ if($inQ -and $i+1 -lt $line.Length -and $line[$i+1] -eq '"'){ $cur += '"'; $i++ } else { $inQ = -not $inQ } }
          elseif($c -eq ',' -and -not $inQ){ $quoteParts += $cur; $cur = "" }
          else { $cur += $c }
        }
        $quoteParts += $cur
        if($quoteParts.Count -lt 8){ continue }
        $tus = [long]$quoteParts[0]
        $htype = $quoteParts[1]
        $opid = 0
        [int]::TryParse($quoteParts[5], [ref]$opid) | Out-Null
        $oproc = $quoteParts[6]
        $mpath = $quoteParts[7]
        $note  = if($quoteParts.Count -ge 9){ $quoteParts[8] } else { "" }
        if($opid -eq $pidE -and ($mpath -match "DIYcomponent" -or $mpath -match "CeTools" -or $mpath -match "iDraw" -or $note -match "STALE|WILD|NO loaded module"))
        {
          $staleEntries += [pscustomobject]@{ tus=$tus; htype=$htype; pid=$opid; proc=$oproc; path=$mpath; note=$note }
          if($tus -gt $maxTusStale){ $maxTusStale = $tus }
        }
      }
    }
  }
  Write-Host ("  e.exe alive marker last seen t_us = {0} (e.exe definitely gone after this)" -f $maxTusAlive)
  if($staleEntries.Count -gt 0)
  {
    $staleHooksFound++
    Write-Host ("  FOUND {0} STALE HOOK ENTRIES from dead e.exe PID={1}. LAST stale entry at t_us={2}." -f $staleEntries.Count,$pidE,$maxTusStale)
    if($maxTusAlive -gt 0 -and $maxTusStale -gt $maxTusAlive)
    {
      $linger = ($maxTusStale - $maxTusAlive)/1000.0
      Write-Host ("  !!! HOOK LINGERED {0:F1} ms AFTER e.exe DIED. THIS IS THE {1:F0}ms MOUSE FREEZE!" -f $linger, $linger)
    }
    $staleEntries | Select-Object -Last 10 | ForEach-Object { Write-Host ("     tus={0} {1} PID={2} path={3} note={4}" -f $_.tus, $_.htype, $_.pid, $_.path, $_.note) }
  }
  else { Write-Host ("  No stale DIYcomponent/CeTools e.exe hooks in enumerator log (did hookProc column decode correctly? Check CSV raw: $enumLog)") }
  Write-Host ""
}

L ("Stale-wild hook cycles detected = {0}/3." -f $staleHooksFound)
if($staleHooksFound -gt 0)
{
  L "SUCCESS: We identified the exact DLL (DIYcomponent/CeTools/...) whose hook callback is lingering X ms AFTER e.exe terminated = the 3.8s freeze source. Next step: inject UnhookWindowsHookEx on close, or reorder DLL shutdown in the FNE."
  exit 1
}
else
{
  L "Stale-hook column not visible in CSV; likely tagHOOK layout mismatch on this Windows build. Fallback: open the raw hooks_cycle*.csv + mouse_cycle*.log and manually cross-reference timestamps by column 1 (both use QPC us from process start, offsets ~0.5s between them)."
  exit 0
}
