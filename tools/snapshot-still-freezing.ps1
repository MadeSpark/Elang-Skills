<#
ASCII-safe snapshot for "still freezes after fix".
Self-contained: uses $PSScriptRoot -> no hardcoded Chinese path -> no 娴嬭瘯 encoding fail.
Records:
  A) verify-eide-kb-freeze output
  B) processes currently holding CeTools/DIYcomponent/iDraw (hook holders after IDE close)
  C) last 12 AppCrash_e.exe WER reports (new crash after fix = CeTools still blowing)
#>
$ErrorActionPreference = "Continue"
$ScriptDir = $PSScriptRoot
if(-not $ScriptDir){ $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$Verify = Join-Path $ScriptDir "verify-eide-kb-freeze.ps1"

Write-Host "==== SNAPSHOT A: verify-eide-kb-freeze ===="
Write-Host "Verify script path = $Verify (exists=$(Test-Path $Verify))"
& powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $Verify
Write-Host ("Verify exit={0}`n" -f $LASTEXITCODE)

Write-Host "==== SNAPSHOT B: Processes currently loading CeTools/DIYcomponent/iDraw/miniblink/WKE DLLs ===="
$targets = @('CeTools','DIYcomponent','iDraw','miniblink','MbKit','WKE')
$found = New-Object System.Collections.Generic.List[object]
foreach($p in (Get-Process -ErrorAction 0)){
  $names = $null
  try {
    $loaded = @($p.Modules)
    $hits = @()
    foreach($m in $loaded){
      foreach($t in $targets){
        if($m.ModuleName -like "*$t*"){ $hits += $m.ModuleName ; break }
      }
    }
    if($hits.Count){ $found.Add([PSCustomObject]@{ PID=$p.Id; ProcessName=$p.ProcessName; StartTime=$p.StartTime; DLLs=($hits -join '|') }) }
  } catch {}
}
if($found.Count -eq 0){ Write-Host "[PASS] No live process holds CeTools/DIYcomponent/iDraw DLLs (expected after IDE closed)" }
else { $found | Format-Table -AutoSize | Out-String -Width 240 }
Write-Host ""

Write-Host "==== SNAPSHOT C: Last 12 AppCrash_e.exe WER times (new crash after fix = crash root cause STILL active) ===="
$paths = @("$env:ProgramData\Microsoft\Windows\WER\ReportArchive","$env:LOCALAPPDATA\CrashDumps")
$all = @()
foreach($pa in $paths){ if(Test-Path $pa){ $all += Get-ChildItem $pa -Filter "*e.exe*" -ErrorAction 0 } }
if(-not $all){ Write-Host "No AppCrash_e.exe entries on record (good)" }
else {
  $all | Sort-Object LastWriteTime -Descending | Select-Object -First 12 LastWriteTime, FullName | Format-Table -AutoSize | Out-String -Width 240
  $newest = $all | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  Write-Host ("Newest e.exe crash event at: {0}" -f $newest.LastWriteTime)
  Write-Host "If newest is AFTER DIY unification (your latest fix run) => crash root cause STILL alive. Otherwise crash root cause FIXED."
}
Write-Host ""
Write-Host "--- end of snapshot ---"
