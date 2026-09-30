param(
  [string]$WriteManifest,
  [string]$BaselineManifest,
  [datetime]$Since = (Get-Date).AddMinutes(-10)
)
$ErrorActionPreference="Stop"
$fail=0; $unknown=0
function Info([string]$message) { Write-Host "[GUARD] $message" }
function Bad([string]$message) { $script:fail++; Write-Host "[FAIL] $message" }
function Unknown([string]$message) { $script:unknown++; Write-Host "[UNKNOWN] $message" }
$protected=@("D:\ides\e\lib\iDraw.fne","D:\ides\e\lib\iDraw\CeTools.dll","D:\ides\e\lib\iDraw")
foreach($path in $protected) { if(!(Test-Path -LiteralPath $path)) { Bad "Missing protected component: $path" } }
if(Test-Path -LiteralPath "D:\ides\e\lib\elang_addin.fne") { Bad "elang_addin.fne is resident; not removing it automatically" }
$manifest=@()
if($fail -eq 0) {
  $files=@(Get-Item -LiteralPath $protected[0]) + @(Get-ChildItem -LiteralPath $protected[2] -Recurse -File)
  foreach($file in $files) {
    try { $manifest += [pscustomobject]@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256 -ErrorAction Stop).Hash;length=$file.Length} }
    catch { Unknown ("Hash read failed: "+$file.FullName) }
  }
  Info ("Protected manifest entries: "+$manifest.Count)
}
if($WriteManifest -and $fail -eq 0 -and $unknown -eq 0) {
  if(Test-Path -LiteralPath $WriteManifest) { throw "Refusing to overwrite an existing manifest" }
  $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $WriteManifest -Encoding UTF8
  Info "Wrote diagnostic hash baseline (installation unchanged)"
}
if($BaselineManifest) {
  if(!(Test-Path -LiteralPath $BaselineManifest)) { Unknown "Baseline manifest missing" }
  else {
    $before=Get-Content -Raw -LiteralPath $BaselineManifest | ConvertFrom-Json
    $diff=@(Compare-Object $before $manifest -Property path,sha256,length)
    if($diff.Count) {
      # IDE-maintained data files may change during ordinary operation. Never
      # silently accept changes to executable components.
      foreach($change in $diff) {
        if([IO.Path]::GetExtension($change.path) -in @(".dll",".exe",".fne")) { Bad ("Protected binary changed: "+$change.path) }
        else { Unknown ("Protected data changed during run: "+$change.path) }
      }
    } else { Info "All protected files match baseline hashes" }
  }
}
foreach($hive in @("HKCU","HKLM")) {
  $path="$hive`:\Control Panel\Desktop"
  try {
    if(!(Test-Path -LiteralPath $path)) { Info "$hive Desktop key absent; unchanged"; continue }
    $key=Get-Item -LiteralPath $path
    if($key.GetValueNames() -contains "LowLevelHooksTimeout") {
      Info ("{0} LowLevelHooksTimeout={1} type={2}; unchanged, effective timeout not inferred" -f $hive,$key.GetValue("LowLevelHooksTimeout"),$key.GetValueKind("LowLevelHooksTimeout"))
    } else { Info "$hive LowLevelHooksTimeout absent; effective timeout not inferred" }
  } catch { Unknown ("Registry read failed: "+$_.Exception.Message) }
}
foreach($process in @(Get-Process e -ErrorAction SilentlyContinue)) {
  try {
    if($process.Threads.Count -eq 0) { Info ("Zero-thread process entry PID="+$process.Id+"; not proof of live hooks") }
    else { Info ("Live e.exe PID="+$process.Id+"; not automatically classified as a zombie") }
  } catch { Unknown ("Process read failed PID="+$process.Id) }
}
try {
  $events=Get-WinEvent -FilterHashtable @{LogName="Application";Id=1000,1001;StartTime=$Since} -ErrorAction Stop
  $hits=@($events | Where-Object { $_.Message -match '(?i)(\be\.exe\b|D:\\ides\\e\\)' })
  foreach($event in $hits) { Info ("Relevant event ID={0} time={1:o} record={2}" -f $event.Id,$event.TimeCreated,$event.RecordId) }
  if(!$hits.Count) { Info "No matching IDE crash events; this is not an input-latency result" }
} catch {
  if($_.FullyQualifiedErrorId -like "NoMatchingEventsFound*") { Info "No matching events; this is not proof of hook cleanup" }
  else { Unknown ("Event query failed: "+$_.Exception.Message) }
}
Info "Static checks do not certify mouse/keyboard responsiveness."
if($fail) { exit 1 }
if($unknown) { exit 2 }
exit 0
