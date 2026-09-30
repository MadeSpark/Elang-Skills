param(
  [ValidateRange(1,20)][int]$Cycles = 3,
  [ValidateRange(1,1000)][int]$ThresholdMs = 500,
  [ValidateSet("Auto","Baseline","ReceiverDelay","SelfTest","Physical")][string]$Mode = "Auto",
  [ValidateRange(0,2000)][int]$DelayMs = 0
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$hl = Join-Path $PSScriptRoot "HookLatency"
$capture = Join-Path $root ".dbg"
$exe = Join-Path $hl "MouseRateSampler.exe"
$source = Join-Path $hl "MouseRateSampler.cs"
$idePath = "D:\ides\e\e.exe"
$run = "pre-fix-" + (Get-Date -Format "yyyyMMdd-HHmmss")
$manifest = Join-Path $capture ($run + "-idraw.json")
$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
function Log([string]$message) { Write-Host ("[INPUT {0}] {1}" -f (Get-Date -Format "HH:mm:ss.fff"),$message) }
if(!(Test-Path -LiteralPath $capture)) { throw "Missing existing diagnostic directory: $capture" }
& $csc /nologo /platform:x86 /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll "/out:$exe" $source
if($LASTEXITCODE -ne 0) { throw "Sampler compilation failed" }
if($Mode -eq "SelfTest") { & $exe --self-test; exit $LASTEXITCODE }
& (Join-Path $PSScriptRoot "verify-eide-kb-freeze.ps1") -WriteManifest $manifest
if($LASTEXITCODE -ne 0) { throw "Installation guards did not pass; no IDE started" }

Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class InputTestWindows {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder b,int n);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder b,int n);
  [DllImport("psapi.dll",SetLastError=true)] static extern bool EnumProcessModulesEx(IntPtr p,IntPtr[] modules,uint bytes,out uint needed,uint filter);
  [DllImport("psapi.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetModuleFileNameEx(IntPtr p,IntPtr m,StringBuilder b,int size);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr p,uint flags,StringBuilder b,ref int size);
  public static string Image(IntPtr process) {
    int n=32768; var b=new StringBuilder(n);
    if(!QueryFullProcessImageName(process,0,b,ref n)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    return b.ToString();
  }
  public static string[] Modules(IntPtr process) {
    var modules=new IntPtr[4096]; uint needed;
    if(!EnumProcessModulesEx(process,modules,(uint)(modules.Length*IntPtr.Size),out needed,3))
      throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    if(needed>modules.Length*IntPtr.Size) throw new Exception("Module buffer incomplete");
    var names=new List<string>();
    for(int i=0;i<needed/IntPtr.Size;i++) {
      var b=new StringBuilder(32768);
      if(GetModuleFileNameEx(process,modules[i],b,b.Capacity)==0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
      names.Add(b.ToString());
    }
    return names.ToArray();
  }
  public static string[] Dialogs(int target) {
    var result=new List<string>();
    EnumWindows(delegate(IntPtr h,IntPtr p) {
      uint pid; GetWindowThreadProcessId(h,out pid);
      if(pid==target && IsWindowVisible(h)) {
        var c=new StringBuilder(256); GetClassName(h,c,256);
        if(c.ToString()=="#32770") { var b=new StringBuilder(512); GetWindowText(h,b,512); result.Add(b.ToString()); }
      }
      return true;
    },IntPtr.Zero);
    return result.ToArray();
  }
}
'@
function Send-Marker([string]$pipeName,[string]$text) {
  $pipe = New-Object IO.Pipes.NamedPipeClientStream(".", $pipeName, [IO.Pipes.PipeDirection]::InOut)
  try {
    $pipe.Connect(3000)
    $writer = New-Object IO.StreamWriter($pipe, (New-Object Text.UTF8Encoding($false)), 1024, $true)
    $reader = New-Object IO.StreamReader($pipe, [Text.Encoding]::UTF8, $false, 1024, $true)
    try {
      $writer.AutoFlush=$true; $writer.WriteLine($text)
      $answer=$reader.ReadLine()
      if($answer -notin @("READY","ACK")) { throw "Invalid marker response: $answer" }
    } finally { $writer.Dispose(); $reader.Dispose() }
  } finally { $pipe.Dispose() }
}
function Assert-Identity($process,$startTime) {
  $process.Refresh()
  if($process.HasExited) { throw "Test IDE exited unexpectedly" }
  if($process.StartTime -ne $startTime -or [InputTestWindows]::Image($process.Handle) -ine $idePath) { throw "Process identity changed" }
  $dialogs = @([InputTestWindows]::Dialogs($process.Id))
  if($dialogs.Count) { throw "IDE dialog detected; not dismissing or saving. Titles=$($dialogs -join '|')" }
}
$result=0
for($cycle=1; $cycle -le $Cycles; $cycle++) {
  $sampler=$null; $ide=$null; $cycleValid=$true
  $pipeName="eide-input-"+[Guid]::NewGuid().ToString("N")
  $prefix=Join-Path $capture ("{0}-cycle{1}" -f $run,$cycle)
  try {
    if($Mode -in @("Auto","Physical")) {
      $running=@(Get-Process e -ErrorAction SilentlyContinue | Where-Object {$_.Threads.Count -gt 0})
      if($running.Count) { throw "Existing live e.exe detected; refusing to touch it" }
    }
    $inputMode=if($Mode -eq "Physical"){"physical"}else{"synthetic"}
    $sampler=Start-Process $exe -ArgumentList @($pipeName,('"{0}"' -f $prefix),"$run-cycle$cycle",$ThresholdMs,$inputMode,180) `
      -RedirectStandardOutput ($prefix+".stdout.log") -RedirectStandardError ($prefix+".stderr.log") -PassThru
    $ready=[Diagnostics.Stopwatch]::StartNew()
    do {
      if($sampler.HasExited) { throw "Sampler exited during initialization" }
      if((Test-Path ($prefix+".stdout.log")) -and (Select-String -LiteralPath ($prefix+".stdout.log") -Pattern "^READY " -Quiet)) { break }
      Start-Sleep -Milliseconds 100
    } while($ready.Elapsed.TotalSeconds -lt 15)
    Send-Marker $pipeName "STATUS"
    Send-Marker $pipeName "BASELINE"
    Log "Cycle $cycle baseline. Dedicated test window receives F24 only; no clicks."
    if($Mode -in @("Baseline","ReceiverDelay")) {
      if($Mode -eq "ReceiverDelay") {
        Start-Sleep -Seconds 2
        Send-Marker $pipeName "DELAY:$DelayMs"
      }
      Start-Sleep -Seconds 10
    } else {
      Start-Sleep -Seconds 5
      Send-Marker $pipeName "STARTUP"
      Log "Cycle $cycle starting original e.exe"
      $ide=Start-Process -FilePath $idePath -WorkingDirectory (Split-Path $idePath) -PassThru
      $identityStart=$ide.StartTime
      @{pid=$ide.Id;startUtcTicks=$identityStart.ToUniversalTime().Ticks;path=[InputTestWindows]::Image($ide.Handle)} |
        ConvertTo-Json | Set-Content -LiteralPath ($prefix+".owned-process.json") -Encoding UTF8
      $wait=[Diagnostics.Stopwatch]::StartNew()
      do {
        Assert-Identity $ide $identityStart
        if($ide.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
      } while($wait.Elapsed.TotalSeconds -lt 30)
      if($ide.MainWindowHandle -eq [IntPtr]::Zero) { throw "No IDE main window within 30s" }
      Send-Marker $pipeName ("RUNNING|"+$ide.Id)
      Log ("IDE pid={0} window-present; not claiming startup completion or hook ownership" -f $ide.Id)
      # Metadata work stays in the controller, never on an input observer thread.
      $modules=@([InputTestWindows]::Modules($ide.Handle) | ForEach-Object { [ordered]@{name=[IO.Path]::GetFileName($_);path=$_} })
      $modules | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($prefix+".modules.json") -Encoding UTF8
      if(-not ($modules | Where-Object {$_.path -like "D:\ides\e\lib\iDraw*"})) { throw "iDraw not observed loaded; cannot accept this test" }
      Send-Marker $pipeName "FOCUS"
      Start-Sleep -Seconds 5
      Assert-Identity $ide $identityStart
      Send-Marker $pipeName ("CLOSE_REQUEST|"+$ide.Id)
      Log ("Request normal close pid={0}; input sampling remains active" -f $ide.Id)
      if(-not $ide.CloseMainWindow()) { throw "CloseMainWindow returned false" }
      $wait.Restart()
      while(-not $ide.HasExited -and $wait.Elapsed.TotalSeconds -lt 30) {
        Assert-Identity $ide $identityStart
        Start-Sleep -Milliseconds 100
      }
      if(-not $ide.HasExited) { throw "IDE did not exit in 30s; left intact for investigation (no force kill)" }
      Send-Marker $pipeName ("EXIT_OBSERVED|"+$ide.Id)
      if($ide.ExitCode -ne 0) { $cycleValid=$false; Log ("Abnormal IDE exit code: "+$ide.ExitCode) }
      Send-Marker $pipeName "POST_EXIT"
      Send-Marker $pipeName "FOCUS"
      Start-Sleep -Seconds 5
    }
  } catch {
    $cycleValid=$false
    Log ("INCONCLUSIVE: "+$_.Exception.Message)
  } finally {
    if($sampler -and -not $sampler.HasExited) {
      try { Send-Marker $pipeName "STOP" } catch { Log ("Sampler STOP error: "+$_.Exception.Message); $cycleValid=$false }
      if(-not $sampler.WaitForExit(15000)) { Log "Sampler safety deadline will stop its own process; no external force kill"; $cycleValid=$false }
    }
    if(Test-Path ($prefix+".json")) {
      $summary=Get-Content -Raw -LiteralPath ($prefix+".json") | ConvertFrom-Json
      $summary.summaries | ForEach-Object { Log ("{0} {1}: {2}, max={3:N1}ms missing={4} send_gap={5:N1}ms" -f $_.phase,$_.kind,$_.status,$_.max_ms,$_.missing,$_.send_gap_ms) }
      $summary.physical_summaries | ForEach-Object { Log ("PHYSICAL {0} {1}: count={2} queue_age={3}ms arrival_gap={4:N1}ms (gap may include inactivity)" -f $_.phase,$_.kind,$_.count,$_.max_queue_age_ms,$_.max_arrival_gap_ms) }
      $summary.slow_delivery_episodes | ForEach-Object { Log ("SLOW-DELIVERY {0}: span={1:N1}ms slow_events={2} (not uninterrupted input loss)" -f $_.kind,$_.duration_ms,$_.slow_event_count) }
      $summary.heartbeat_stalls | ForEach-Object { Log ("HEARTBEAT {0}: {1:N1}ms" -f $_.source,(1000.0*($_.end-$_.begin)/$summary.frequency)) }
      if($summary.exit_code -eq 1) { $result=1 }
      elseif($summary.exit_code -ne 0 -and $result -eq 0) { $result=2 }
    } else { $cycleValid=$false }
    if(-not $cycleValid -and $result -eq 0) { $result=2 }
  }
  if(-not $cycleValid) { break }
}
& (Join-Path $PSScriptRoot "verify-eide-kb-freeze.ps1") -BaselineManifest $manifest
if($LASTEXITCODE -ne 0 -and $result -eq 0) { $result=2 }
Log "Diagnostic exit=$result (0=measured PASS, 1=over threshold, 2=INCONCLUSIVE). This is not a repair."
exit $result
