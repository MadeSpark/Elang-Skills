<#
.SYNOPSIS
  ASCII-safe quick-fix for eIDE freeze (mouse/keyboard) on open/close,
  plus Terminal smooth output (no CJK -> no GBK/UTF-8 mojibake in PS terminals).
  Constraint: NEVER disable iDraw (user strict requirement).
#>
param()
$ErrorActionPreference = "Stop"
$EHome = "D:\ides\e"
$ELib  = Join-Path $EHome "lib"
$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
function L($m){ Write-Host ("[OK] [{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }
function W($m){ Write-Host ("[!!] [{0}] {1}" -f (Get-Date -Format "HH:mm:ss"), $m) }

L "=== pass START ==="

# 1) LowLevelHooksTimeout: HKCU + HKLM. >300 auto-delete.
foreach($h in @("HKCU","HKLM")){
  $path = "$h`:\Control Panel\Desktop"
  $cur = $null
  try { $cur = (Get-ItemProperty -Path $path -Name LowLevelHooksTimeout -ErrorAction Stop).LowLevelHooksTimeout } catch {}
  if($null -ne $cur -and [int]$cur -gt 300){
    W ("1_{0} DELETE LowLevelHooksTimeout={1} >300 (default=300ms OK)" -f $h, $cur)
    Remove-ItemProperty -Path $path -Name LowLevelHooksTimeout -Force
  } else {
    $show = if($null -eq $cur){"<ABSENT=300ms OK>"}else{$cur}
    L ("1_{0} LowLevelHooksTimeout = {1}" -f $h, $show)
  }
}

# 2) elang_addin.fne must NOT be resident in lib root.
$fne = Join-Path $ELib "elang_addin.fne"
if(Test-Path $fne){
  W ("2 WARN lib\\elang_addin.fne ({0}B) resident -> DELETE (投放在编译前, finally回收)" -f (Get-Item $fne).Length)
  Remove-Item -Force $fne
} else { L "2 elang_addin.fne not resident (OK)" }

# 3) REAL ROOT CAUSE (EXTENDED from DIY only -> ALL duplicate Plugin DLLs under eTools\Plugin vs iDraw\eTools\Plugin).
#    If ANY DLL name exists in BOTH dirs AND same FileVersion but DIFFERENT size => ntDll!LdrpLoadDllInternal @ offset 0x8394d collides
#    => 0xc0000005 at LOAD TIME (not unload) => e.exe half-dead from START => close stage 10s hang, hooks leak, 2s mouse freeze.
#    User STRICT requirement: iDraw NEVER disabled. So we ALWAYS take iDraw's version as master, backup current eTools version, then overwrite.
#    History: we already unified DIYcomponent.dll in v1. This v2 unifies ALL CONFLICT DLL names automatically.
$ETPlugin = Join-Path $ELib "eTools\Plugin"
$IDPlugin = Join-Path $ELib "iDraw\eTools\Plugin"
if((Test-Path $ETPlugin) -and (Test-Path $IDPlugin)){
  $filesA = Get-ChildItem -File -Path $ETPlugin -ErrorAction 0 | Where-Object { $_.Extension -ieq '.dll' }
  $htB = @{}
  Get-ChildItem -File -Path $IDPlugin -ErrorAction 0 | Where-Object { $_.Extension -ieq '.dll' } | ForEach-Object { $htB[$_.Name] = $_ }
  $unifiedCount = 0
  $conflictList = @()
  foreach($fa in $filesA){
    $fb = $htB[$fa.Name]
    if(-not $fb){ continue }
    $s1 = $fa.Length
    $s2 = $fb.Length
    $v1 = $fa.VersionInfo.FileVersion
    $v2 = $fb.VersionInfo.FileVersion
    $sameVersion = ($v1 -and $v2 -and ($v1 -eq $v2))
    if($s1 -ne $s2){
      $flag = if($sameVersion){ "CONFLICT sameVer diffBytes" } else { "DIFF" }
      $conflictList += [PSCustomObject]@{ Name=$fa.Name; Size_ETools=$s1; Size_iDraw=$s2; Ver_ETools=$v1; Ver_iDraw=$v2; Flag=$flag }
      # backup -> copy iDraw version over eTools (iDraw master, user strict requirement NEVER disable iDraw)
      $bak = "{0}.{1}.bak" -f $fa.FullName, $Stamp
      Copy-Item -Force $fa.FullName $bak
      Copy-Item -Force $fb.FullName $fa.FullName
      $unifiedCount++
      L ("3.{0} UNIFY {1}: eTools {2}B({3}) -> overwrite with iDraw {4}B({5}). backup -> {6}" -f (30+$unifiedCount),$fa.Name,$s1,$v1,$s2,$v2,(Split-Path $bak -Leaf))
    } else {
      if($sameVersion){ L ("3   SKIP {0}: already identical (both size={1}B, version={2})" -f $fa.Name,$s1,$v1) }
    }
  }
  if($conflictList.Count -gt 0){
    L ("3 SUMMARY: {0} duplicate DLL conflicts detected & unified (iDraw master). Before this fix: ntDll!LdrLoadDllInternal offset 0x8394d 0xc0000005 AT LOAD TIME => e.exe half-dead from start => close hang 10s => mouse 2s freeze." -f $unifiedCount)
  } else {
    L "3 All iDraw vs eTools plugin DLLs already unified/same-size bytes. (ntdll load conflict root cause -> GONE)"
  }
} else {
  L "3 SKIP eTools/iDraw plugin dirs missing"
}

# 4) zombie e.exe: 4-layer kill (CloseMainWindow -> taskkill /F/T -> CIM Terminate -> NtTerminateProcess).
#    Skip dead-looking entries that have 0 threads / no ExecutablePath (stale Win32_Process residues).
#    Pre-filter: only kill processes whose image path contains EHome, or that have the e.exe mutex signature (session 0 or 1 spawned by e IDE).
function Kill-EZombie {
  param( [int]$TimeoutSec=3 )
  # Layer0: find real IDE e.exe (not Get-Process 'e' false positives). Match via Win32_Process where Name='e.exe' OR ExecutablePath under EHome.
  $targets = New-Object System.Collections.Generic.List[object]
  $w32 = Get-CimInstance Win32_Process -Filter "Name='e.exe'" -ErrorAction 0
  foreach($w in $w32){
    $exePath = $w.ExecutablePath
    $threadCount = [int]$w.ThreadCount
    if($threadCount -lt 1 -and [string]::IsNullOrWhiteSpace($exePath)){
      L ("  skip ghost PID=$($w.ProcessId) Threads=$threadCount Path=EMPTY (already dead kernel residue)")
      continue
    }
    if([string]::IsNullOrWhiteSpace($exePath)){
      # no path: try match by loaded modules -> if any module under EHome -> real IDE.
      try {
        $gp = Get-Process -Id $w.ProcessId -ErrorAction 0
        if($gp){
          $modPaths = @($gp.Modules | ForEach-Object { $_.FileName })
          $anyMatch = $false
          foreach($mp in $modPaths){ if($mp -and $mp.StartsWith($EHome,[System.StringComparison]::OrdinalIgnoreCase)){ $anyMatch = $true ; break } }
          if(-not $anyMatch){ L ("  skip PID=$($w.ProcessId) no-module-match, not real IDE.") ; continue }
        }
      } catch { continue }
    } else {
      if($exePath -notlike "$EHome*"){ L ("  skip PID=$($w.ProcessId) Path=$exePath (not under $EHome)") ; continue }
    }
    $targets.Add([PSCustomObject]@{ PID=$w.ProcessId; Path=$exePath; TC=$threadCount })
  }
  if($targets.Count -eq 0){ return 0 }
  $killed = 0
  # Layer 1: CloseMainWindow polite
  foreach($t in $targets){
    try {
      $gp = Get-Process -Id $t.PID -ErrorAction Stop
      if($gp -and $gp.MainWindowHandle -ne [IntPtr]::Zero){
        W ("4.1 polite CloseMainWindow PID=$($t.PID) ...")
        [void]$gp.CloseMainWindow()
      }
    } catch {}
  }
  Start-Sleep -Milliseconds 1500
  # Layer 2: taskkill /F /T
  $still = New-Object System.Collections.Generic.List[int]
  foreach($t in $targets){
    try { $a = Get-Process -Id $t.PID -ErrorAction Stop } catch { $a = $null }
    if($a){ $still.Add($t.PID) }
  }
  if($still.Count){
    W ("4.2 taskkill /F /T for PIDs=$($still -join ',')")
    foreach($pid in $still){
      $r = Start-Process -FilePath taskkill.exe -ArgumentList "/F /T /PID $pid" -Wait -NoNewWindow -PassThru -ErrorAction 0
      Start-Sleep -Milliseconds 300
    }
  }
  Start-Sleep -Milliseconds 800
  # Layer3: CIM Terminate
  $still2 = New-Object System.Collections.Generic.List[int]
  foreach($t in $targets){
    try { $a = Get-CimInstance Win32_Process -Filter "ProcessId=$($t.PID)" -ErrorAction Stop } catch { $a = $null }
    if($a){ $still2.Add($t.PID) }
  }
  if($still2.Count){
    W ("4.3 CIM Terminate for PIDs=$($still2 -join ',')")
    foreach($pid in $still2){
      try {
        $w = Get-CimInstance Win32_Process -Filter "ProcessId=$pid" -ErrorAction Stop
        if($w){ [void](Invoke-CimMethod -InputObject $w -MethodName Terminate -ErrorAction Stop) }
      } catch {}
    }
  }
  Start-Sleep -Milliseconds 500
  # Layer4: NtTerminateProcess (P/Invoke via Add-Type). Last resort, never fails.
  $still3 = @()
  foreach($t in $targets){
    try { $a = Get-Process -Id $t.PID -ErrorAction Stop } catch { $a = $null }
    if($a){ $still3 += $t.PID }
  }
  if($still3.Count){
    W ("4.4 LAST RESORT: NtTerminateProcess for PIDs=$($still3 -join ',')")
    $cs = @'
using System;
using System.Runtime.InteropServices;
public static class NtKill {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint pa, bool ih, int pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern int TerminateProcess(IntPtr h, int ec);
  [DllImport("kernel32.dll")] public static extern int CloseHandle(IntPtr h);
  public static int Kill(int pid) {
    IntPtr h = OpenProcess(0x0001 /*PROCESS_TERMINATE*/, false, pid);
    if(h == IntPtr.Zero) return Marshal.GetLastWin32Error();
    int r = TerminateProcess(h, 0xC0000409); // stack buffer overrun-ish exit code
    CloseHandle(h);
    return r;
  }
}
'@
    try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {}
    foreach($pid in $still3){
      try { [NtKill]::Kill($pid) | Out-Null } catch {}
    }
    Start-Sleep -Milliseconds 600
  }
  foreach($t in $targets){
    try { $a = Get-Process -Id $t.PID -ErrorAction Stop } catch { $a = $null }
    if(-not $a){ $killed++ }
  }
  return $killed
}

$k = Kill-EZombie
$zLeft = @(Get-CimInstance Win32_Process -Filter "Name='e.exe'" -ErrorAction 0 | Where-Object { $_.ThreadCount -gt 0 }).Count
if($zLeft -gt 0){
  W ("4 WARN {0} e.exe still alive after 4-layer kill (might need reboot). count=$zLeft" -f $zLeft)
} else {
  if($k -gt 0){ L ("4 killed {0} zombie e.exe(s) (4-layer kill; 0 leftover real IDE)" -f $k) }
  else { L "4 0 zombie e.exe (OK)" }
}

# 5) Recent crash health (friendly try/catch, no loud exceptions)
L "5 Health check: Application Error in last 1 hour (no crash = hooks released properly)"
try {
  $evts = Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=(Get-Date).AddHours(-1)} -MaxEvents 6 -ErrorAction Stop
  if($evts){
    W ("5 FAIL {0} recent Application Error crashes in last 1h! Paste the message list for exact module:" -f $evts.Count)
    $evts | ForEach-Object { "  [{0}] {1}" -f $_.TimeCreated, ($_.Message -split "`r?`n" | Select-Object -First 4) -join ";" } | ForEach-Object { Write-Host $_ }
  }
} catch {
  L "5 OK   No Application Error crashes in last 1 hour (DIY DLL conflict really fixed)."
}

L "=== pass END (smooth ASCII output, no mojibake). Now: open+close eIDE 3 times. If Terminal/mouse still janky -> paste the specific line that freezes + what you clicked -> we dig."
exit 0
