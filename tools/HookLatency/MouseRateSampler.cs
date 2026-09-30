// Independent input observers. User-mode arrival times are not HID interrupt times.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Drawing;

namespace MouseRate
{
    static class Program
    {
        [StructLayout(LayoutKind.Sequential)] struct POINT { public int x, y; }
        [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, flags, time; public UIntPtr extra; }
        [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public UIntPtr extra; }
        [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public MOUSEINPUT mouse; [FieldOffset(0)] public KEYBDINPUT key; }
        [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNION u; }
        [StructLayout(LayoutKind.Sequential)] struct MSLL { public POINT pt; public uint data, flags, time; public UIntPtr extra; }
        [StructLayout(LayoutKind.Sequential)] struct KBLL { public uint vk, scan, flags, time; public UIntPtr extra; }
        [StructLayout(LayoutKind.Sequential)] struct RID { public ushort page, usage; public uint flags; public IntPtr target; }
        [StructLayout(LayoutKind.Sequential)] struct RAWHEADER { public uint type, size; public IntPtr device, param; }
        [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public UIntPtr wp; public IntPtr lp; public uint time; public POINT point; public uint privateValue; }
        delegate IntPtr Hook(int code, IntPtr wp, IntPtr lp);
        [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint count, INPUT[] input, int size);
        [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SetWindowsHookEx(int id, Hook callback, IntPtr module, uint thread);
        [DllImport("user32.dll", SetLastError=true)] static extern bool UnhookWindowsHookEx(IntPtr hook);
        [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wp, IntPtr lp);
        [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hwnd);
        [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT point);
        [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT point);
        [DllImport("user32.dll")] static extern IntPtr GetMessageExtraInfo();
        [DllImport("user32.dll")] static extern int GetMessageTime();
        [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint mapType);
        [DllImport("user32.dll")] static extern bool PeekMessage(out MSG msg, IntPtr hwnd, uint min, uint max, uint remove);
        [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
        [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG msg);
        [DllImport("user32.dll")] static extern uint MsgWaitForMultipleObjectsEx(uint count, IntPtr handles, uint timeout, uint mask, uint flags);
        [DllImport("user32.dll", SetLastError=true)] static extern bool RegisterRawInputDevices(RID[] devices, uint count, uint size);
        [DllImport("user32.dll", SetLastError=true)] static extern uint GetRawInputData(IntPtr raw, uint command, out RAWHEADER header, ref uint size, uint headerSize);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();

        sealed class Sample
        {
            public int id; public string kind, phase;
            public long sent, returned, hook, received;
            public bool expectWindow; public uint apiCount, flags, eventTime;
        }
        sealed class Marker { public string phase; public long qpc; public int targetPid; }
        sealed class Observation { public string kind; public long qpc; public uint flags, eventTime, queueAgeMs; }
        sealed class Interval { public long begin, end; public string source; public int count; }
        sealed class ThreadInfo { public string role; public uint tid; }
        static readonly ConcurrentQueue<ThreadInfo> threadInfo = new ConcurrentQueue<ThreadInfo>();
        static readonly List<Interval> heartbeatStalls = new List<Interval>();
        static readonly long[] lastBeat = new long[4];
        static readonly string[] beatNames = {"receiver_timer","hook_pump","raw_timer","sender"};
        const double slowMs=100, recoveryMs=100;
        static readonly ConcurrentDictionary<int, Sample> samples = new ConcurrentDictionary<int, Sample>();
        static readonly ConcurrentQueue<Observation> observations = new ConcurrentQueue<Observation>();
        static readonly ConcurrentQueue<Marker> markers = new ConcurrentQueue<Marker>();
        static readonly ConcurrentQueue<string> errors = new ConcurrentQueue<string>();
        static readonly ConcurrentQueue<string> debug = new ConcurrentQueue<string>();
        static readonly ManualResetEvent receiverReady = new ManualResetEvent(false), rawReady = new ManualResetEvent(false), hookReady = new ManualResetEvent(false);
        static volatile bool stopping, physicalOnly;
        static volatile string phase = "INIT";
        static int sequence, receiverDelay;
        static long receiverBeat, hookBeat, rawBeat, senderBeat;
        static int keyCalls, mouseCalls;
        static long mouseHookHandle, keyHookHandle;
        static readonly long[] maxBeat = new long[4];
        static Form receiver;
        static IntPtr receiverHwnd;
        static ApplicationContext rawContext;
        static Hook mouseCallback, keyCallback;
        static string runId, output, pipeName, debugUrl = "http://127.0.0.1:7777/event";
        static int threshold = 500, maxSeconds = 180;
        static long start;
        static readonly JavaScriptSerializer json = new JavaScriptSerializer();
        static long Now() { return Stopwatch.GetTimestamp(); }
        static double Ms(long ticks) { return ticks * 1000.0 / Stopwatch.Frequency; }
        static void Fail(string message) { errors.Enqueue(message); }
        static Thread StartThread(ThreadStart action, bool sta)
        {
            Thread t = new Thread(delegate() { try { action(); } catch(Exception e) { Fail(e.ToString()); } });
            t.IsBackground = true;
            if(sta) t.SetApartmentState(ApartmentState.STA);
            t.Start(); return t;
        }
        static int Tag(UIntPtr p)
        {
            ulong n = p.ToUInt64();
            return (n & 0xff000000UL) == 0x45000000UL ? (int)(n & 0xffffffUL) : 0;
        }
        static IntPtr OnHook(int code, IntPtr wp, IntPtr lp, bool mouse)
        {
            if(mouse) Interlocked.Increment(ref mouseCalls); else Interlocked.Increment(ref keyCalls);
            if(code >= 0)
            {
                long now = Now(); int id; uint flags, time;
                if(mouse) { MSLL x=(MSLL)Marshal.PtrToStructure(lp, typeof(MSLL)); id=Tag(x.extra); flags=x.flags; time=x.time; }
                else { KBLL x=(KBLL)Marshal.PtrToStructure(lp, typeof(KBLL)); id=Tag(x.extra); flags=x.flags; time=x.time; }
                Sample sample;
                if(id != 0 && samples.TryGetValue(id, out sample))
                {
                    Interlocked.Exchange(ref sample.hook, now); sample.flags=flags; sample.eventTime=time;
                }
                else observations.Enqueue(new Observation { kind=mouse?"MOUSE_UNTAGGED":"KEY_UNTAGGED", qpc=now, flags=flags, eventTime=time });
            }
            return CallNextHookEx(IntPtr.Zero, code, wp, lp);
        }
        static void HookLoop()
        {
            threadInfo.Enqueue(new ThreadInfo{role="hook",tid=GetCurrentThreadId()});
            mouseCallback = delegate(int c, IntPtr w, IntPtr l) { return OnHook(c,w,l,true); };
            keyCallback = delegate(int c, IntPtr w, IntPtr l) { return OnHook(c,w,l,false); };
            System.Runtime.CompilerServices.RuntimeHelpers.PrepareDelegate(mouseCallback);
            System.Runtime.CompilerServices.RuntimeHelpers.PrepareDelegate(keyCallback);
            IntPtr hm=IntPtr.Zero, hk=IntPtr.Zero;
            // A dedicated native pump avoids sharing WinForms filtering and startup work.
            MSG msg; PeekMessage(out msg,IntPtr.Zero,0,0,0);
            if(!physicalOnly)
            {
                hm=SetWindowsHookEx(14,mouseCallback,GetModuleHandle(null),0);
                hk=SetWindowsHookEx(13,keyCallback,GetModuleHandle(null),0);
                mouseHookHandle=hm.ToInt64(); keyHookHandle=hk.ToInt64();
                if(hm==IntPtr.Zero || hk==IntPtr.Zero) Fail("SetWindowsHookEx failed: "+Marshal.GetLastWin32Error());
            }
            Interlocked.Exchange(ref hookBeat,Now()); hookReady.Set();
            try {
                while(!stopping) {
                    Interlocked.Exchange(ref hookBeat,Now());
                    while(PeekMessage(out msg,IntPtr.Zero,0,0,1)) { TranslateMessage(ref msg); DispatchMessage(ref msg); }
                    MsgWaitForMultipleObjectsEx(0,IntPtr.Zero,10,0x4ff,4);
                }
            }
            finally { if(hm!=IntPtr.Zero) UnhookWindowsHookEx(hm); if(hk!=IntPtr.Zero) UnhookWindowsHookEx(hk); }
        }
        sealed class RawSink : NativeWindow
        {
            public RawSink()
            {
                CreateHandle(new CreateParams { Caption="InputRawSink", Parent=new IntPtr(-3) });
                RID[] devices = { new RID{page=1,usage=2,flags=0x100,target=Handle}, new RID{page=1,usage=6,flags=0x100,target=Handle} };
                if(!RegisterRawInputDevices(devices,2,(uint)Marshal.SizeOf(typeof(RID)))) Fail("Raw registration failed: "+Marshal.GetLastWin32Error());
            }
            protected override void WndProc(ref Message m)
            {
                if(m.Msg==0xff)
                {
                    uint size=(uint)Marshal.SizeOf(typeof(RAWHEADER)); RAWHEADER h;
                    uint result=GetRawInputData(m.LParam,0x10000005,out h,ref size,size);
                    if(result==0xffffffff) Fail("Raw header failed: "+Marshal.GetLastWin32Error());
                    else observations.Enqueue(new Observation {
                        kind=(h.type==0?"RAW_MOUSE":"RAW_KEY")+(h.device==IntPtr.Zero?"_VIRTUAL":"_DEVICE"),
                        qpc=Now(),eventTime=unchecked((uint)GetMessageTime()),
                        queueAgeMs=unchecked((uint)Environment.TickCount-(uint)GetMessageTime()) });
                }
                base.WndProc(ref m);
            }
        }
        static void RawLoop()
        {
            threadInfo.Enqueue(new ThreadInfo{role="raw",tid=GetCurrentThreadId()});
            RawSink sink=new RawSink(); rawContext=new ApplicationContext();
            using(var timer=new System.Windows.Forms.Timer())
            {
                timer.Interval=10; timer.Tick+=delegate { Interlocked.Exchange(ref rawBeat,Now()); if(stopping) rawContext.ExitThread(); };
                timer.Start(); Interlocked.Exchange(ref rawBeat,Now()); rawReady.Set();
                try { Application.Run(rawContext); } finally { sink.DestroyHandle(); }
            }
        }
        sealed class TestForm : Form
        {
            protected override void WndProc(ref Message m)
            {
                if(m.Msg==0x200 || m.Msg==0x100 || m.Msg==0x101)
                {
                    int delay=Interlocked.Exchange(ref receiverDelay,0);
                    if(delay>0) Thread.Sleep(delay); // Self-test delays only this private receiver, never a hook.
                    int id=Tag(new UIntPtr(unchecked((ulong)GetMessageExtraInfo().ToInt64())));
                    Sample s;
                    if(id!=0 && samples.TryGetValue(id,out s)) Interlocked.CompareExchange(ref s.received,Now(),0);
                    else if(physicalOnly) observations.Enqueue(new Observation{
                        kind=m.Msg==0x200?"WINDOW_MOUSE":"WINDOW_KEY",qpc=Now(),
                        eventTime=unchecked((uint)GetMessageTime()),
                        queueAgeMs=unchecked((uint)Environment.TickCount-(uint)GetMessageTime())});
                }
                base.WndProc(ref m);
            }
        }
        static void ReceiverLoop()
        {
            threadInfo.Enqueue(new ThreadInfo{role="receiver",tid=GetCurrentThreadId()});
            receiver=new TestForm();
            receiver.TopMost=true;
            receiver.Text="Input latency diagnostic - F24 only - no clicks";
            receiver.Size=new Size(420,220); receiver.StartPosition=FormStartPosition.Manual;
            POINT p; GetCursorPos(out p);
            Rectangle screen=Screen.FromPoint(new Point(p.x,p.y)).WorkingArea;
            receiver.Location=new Point(Math.Max(screen.Left,Math.Min(screen.Right-420,p.x-210)),Math.Max(screen.Top,Math.Min(screen.Bottom-220,p.y-110)));
            receiver.BackColor=Color.FromArgb(230,240,250);
            receiver.Shown+=delegate { receiverHwnd=receiver.Handle; receiver.Activate(); receiverReady.Set(); };
            receiver.FormClosing+=delegate { if(!stopping) Fail("Receiver closed before STOP"); stopping=true; };
            using(var timer=new System.Windows.Forms.Timer())
            {
                timer.Interval=10; timer.Tick+=delegate { Interlocked.Exchange(ref receiverBeat,Now()); if(stopping) receiver.Close(); };
                timer.Start(); Interlocked.Exchange(ref receiverBeat,Now()); Application.Run(receiver);
            }
        }
        static INPUT Prepare(bool mouse, bool up, bool expect, int direction, out Sample s)
        {
            int id=Interlocked.Increment(ref sequence);
            if(id>=0xffffff) throw new InvalidOperationException("Tag range exhausted");
            s=new Sample{id=id,kind=mouse?"MOUSE":(up?"KEY_UP":"KEY_DOWN"),phase=phase,sent=Now(),expectWindow=expect};
            samples[id]=s;
            UIntPtr tag=new UIntPtr((uint)(0x45000000|id));
            INPUT input=new INPUT(); input.type=mouse?0U:1U;
            if(mouse) input.u.mouse=new MOUSEINPUT{dx=direction*3,flags=0x2001,extra=tag}; // MOVE_NOCOALESCE; avoid subpixel rounding to zero.
            else input.u.key=new KEYBDINPUT{vk=0x87,scan=(ushort)MapVirtualKey(0x87,0),flags=up?2U:0U,extra=tag};
            return input;
        }
        static void SenderLoop()
        {
            threadInfo.Enqueue(new ThreadInfo{role="sender",tid=GetCurrentThreadId()});
            int direction=1;
            while(!stopping)
            {
                Interlocked.Exchange(ref senderBeat,Now());
                if(!physicalOnly && phase!="INIT")
                {
                    bool foreground=GetForegroundWindow()==receiverHwnd;
                    POINT point; GetCursorPos(out point);
                    bool over=WindowFromPoint(point)==receiverHwnd;
                    List<INPUT> batch=new List<INPUT>(); List<Sample> created=new List<Sample>(); Sample s;
                    batch.Add(Prepare(true,false,over,direction,out s)); created.Add(s); direction=-direction;
                    if(foreground)
                    {
                        batch.Add(Prepare(false,false,true,0,out s)); created.Add(s);
                        batch.Add(Prepare(false,true,true,0,out s)); created.Add(s);
                    }
                    uint count=SendInput((uint)batch.Count,batch.ToArray(),Marshal.SizeOf(typeof(INPUT)));
                    long end=Now();
                    foreach(Sample x in created) { x.apiCount=count; Interlocked.Exchange(ref x.returned,end); }
                    if(count!=batch.Count) { Fail("SendInput short write: "+count+"/"+batch.Count+" gle="+Marshal.GetLastWin32Error()); stopping=true; }
                }
                Thread.Sleep(20);
            }
        }
        //region debug-point H1-H4:lifecycle
        static void Mark(string name, int pid)
        {
            Marker marker=new Marker{phase=name,qpc=Now(),targetPid=pid}; markers.Enqueue(marker); phase=name;
            debug.Enqueue(new JavaScriptSerializer().Serialize(new {
                sessionId="eide-freeze-crash",runId=runId,hypothesisId="H3",location="MouseRateSampler",
                msg="[DEBUG] lifecycle",data=new{phase=name,qpc=marker.qpc,frequency=Stopwatch.Frequency,targetPid=pid}
            }));
        }
        static void DebugLoop()
        {
            while(!stopping || !debug.IsEmpty)
            {
                string body;
                if(!debug.TryDequeue(out body)) { Thread.Sleep(50); continue; }
                try
                {
                    var request=(HttpWebRequest)WebRequest.Create(debugUrl); request.Method="POST"; request.ContentType="application/json";
                    request.Timeout=1000; request.ReadWriteTimeout=1000;
                    byte[] data=Encoding.UTF8.GetBytes(body);
                    using(Stream stream=request.GetRequestStream()) stream.Write(data,0,data.Length);
                    using(var response=request.GetResponse()) {}
                }
                catch(Exception e) { Fail("Debug report failed: "+e.Message); }
            }
        }
        //endregion
        static void PipeLoop()
        {
            while(!stopping)
            {
                using(var pipe=new NamedPipeServerStream(pipeName,PipeDirection.InOut,1,PipeTransmissionMode.Byte,PipeOptions.None))
                {
                    pipe.WaitForConnection();
                    using(var reader=new StreamReader(pipe,Encoding.UTF8,false,1024,true))
                    using(var writer=new StreamWriter(pipe,new UTF8Encoding(false),1024,true){AutoFlush=true})
                    {
                        string line=reader.ReadLine()??"";
                        if(line=="STOP") { Mark("STOP",0); writer.WriteLine("ACK"); stopping=true; }
                        else if(line=="FOCUS") { receiver.BeginInvoke((Action)delegate { receiver.Activate(); SetForegroundWindow(receiverHwnd); }); writer.WriteLine("ACK"); }
                        else if(line.StartsWith("DELAY:")) { receiverDelay=int.Parse(line.Substring(6)); writer.WriteLine("ACK"); }
                        else if(line=="STATUS") writer.WriteLine("READY");
                        else { string[] parts=line.Split('|'); int pid=parts.Length>1?int.Parse(parts[1]):0; Mark(parts[0],pid); writer.WriteLine("ACK"); }
                    }
                }
            }
        }
        static double Percentile(List<double> values,double p)
        {
            if(values.Count==0) return 0;
            values.Sort(); return values[Math.Min(values.Count-1,(int)Math.Ceiling(values.Count*p)-1)];
        }
        static int Judge(int count,int missing,double max,double coverage,bool valid)
        {
            if(max>=threshold) return 1;
            if(!valid || count<2 || missing>0 || coverage>=threshold) return 2;
            return 0;
        }
        static List<Interval> MergeIntervals(IEnumerable<Interval> input, double recovery)
        {
            var result=new List<Interval>();
            foreach(var item in input.OrderBy(x=>x.begin))
            {
                if(result.Count==0 || Ms(item.begin-result[result.Count-1].end)>recovery)
                    result.Add(new Interval{begin=item.begin,end=item.end,count=item.count,source=item.source});
                else {
                    var tail=result[result.Count-1];
                    tail.end=Math.Max(tail.end,item.end); tail.count+=item.count;
                }
            }
            return result;
        }
        static object[] Episodes(out bool repeatedFailure)
        {
            var output=new List<object>(); repeatedFailure=false;
            foreach(string kind in new[]{"MOUSE","KEY_DOWN","KEY_UP"})
            {
                var intervals=samples.Values.Where(s=>s.kind==kind && s.expectWindow && s.received>0 &&
                    Ms(s.received-s.sent)>=slowMs).Select(s=>new Interval{begin=s.sent,end=s.received,count=1,source=kind});
                foreach(var span in MergeIntervals(intervals,recoveryMs)) {
                    double duration=Ms(span.end-span.begin);
                    if(duration>=threshold) repeatedFailure=true;
                    output.Add(new{kind=kind,begin_qpc=span.begin,end_qpc=span.end,duration_ms=duration,slow_event_count=span.count});
                }
            }
            return output.ToArray();
        }
        static void ObserveBeats(long now,long[] beats)
        {
            for(int i=0;i<beats.Length;i++) {
                if(beats[i]<=0) continue;
                maxBeat[i]=Math.Max(maxBeat[i],Math.Max(0,now-beats[i]));
                if(lastBeat[i]>0 && beats[i]!=lastBeat[i]) {
                    long elapsed=beats[i]-lastBeat[i];
                    maxBeat[i]=Math.Max(maxBeat[i],elapsed);
                    if(Ms(elapsed)>=slowMs) heartbeatStalls.Add(new Interval{source=beatNames[i],begin=lastBeat[i],end=beats[i],count=1});
                }
                lastBeat[i]=beats[i];
            }
        }
        static object Summarize(string name,long begin,long end,string kind,out int verdict)
        {
            // Attribute cross-boundary stalls to the phase containing their send, and
            // include pending events from before this phase whose delivery overlaps it.
            Sample[] all=samples.Values.Where(s=>s.kind==kind && s.sent<end &&
                (s.sent>=begin || Math.Max(Math.Max(s.hook,s.received),s.returned)>=begin ||
                (s.hook==0 && s.received==0) || (s.expectWindow && s.received==0))).OrderBy(s=>s.sent).ToArray();
            var delays=new List<double>(); var windowDelays=new List<double>(); var callDelays=new List<double>();
            int missing=0,windowCount=0,hookCount=0,windowMissing=0,observerMissing=0;
            long prev=begin; double gap=0;
            foreach(Sample s in all)
            {
                if(s.sent>=begin) { gap=Math.Max(gap,Ms(s.sent-prev)); prev=s.sent; }
                if(s.hook==0 || s.returned==0 || (s.expectWindow && s.received==0)) missing++;
                if(s.hook==0) observerMissing++;
                if(s.expectWindow && s.received==0) windowMissing++;
                if(s.hook>0) hookCount++;
                if(s.received>0) { windowCount++; windowDelays.Add(Ms(s.received-s.sent)); }
                if(s.returned>0) callDelays.Add(Ms(s.returned-s.sent));
                long delivered=Math.Max(Math.Max(s.hook,s.received),s.returned);
                if(delivered>0) delays.Add(Ms(delivered-s.sent));
            }
            gap=Math.Max(gap,Ms(end-prev));
            double max=delays.Count>0?delays.Max():0;
            verdict=Judge(all.Length,missing,max,gap,!physicalOnly && windowCount>=2);
            return new {phase=name,kind=kind,count=all.Length,hook_count=hookCount,window_count=windowCount,missing=missing,
                window_missing=windowMissing,observer_missing=observerMissing,
                max_window_ms=windowDelays.Count>0?windowDelays.Max():0,max_send_call_ms=callDelays.Count>0?callDelays.Max():0,
                max_ms=max,p95_ms=Percentile(delays,.95),p99_ms=Percentile(delays,.99),send_gap_ms=gap,
                exceed_count=delays.Count(x=>x>=threshold),status=verdict==0?"PASS":verdict==1?"FAIL":"INCONCLUSIVE"};
        }
        static int Save()
        {
            Marker[] list=markers.ToArray(); var summaries=new List<object>(); int result=errors.IsEmpty && !physicalOnly?0:2;
            var physicalSummaries=new List<object>();
            for(int i=0;i+1<list.Length;i++)
            {
                if(Ms(list[i+1].qpc-list[i].qpc)<1 || list[i].phase=="EXIT_OBSERVED") continue;
                if(physicalOnly)
                {
                    foreach(string kind in new[]{"RAW_MOUSE_DEVICE","RAW_KEY_DEVICE","WINDOW_MOUSE","WINDOW_KEY"})
                    {
                        Observation[] events=observations.Where(o=>o.kind==kind && o.qpc>=list[i].qpc && o.qpc<list[i+1].qpc).OrderBy(o=>o.qpc).ToArray();
                        long prev=list[i].qpc; double gap=0;
                        foreach(var e in events) { gap=Math.Max(gap,Ms(e.qpc-prev)); prev=e.qpc; }
                        gap=Math.Max(gap,Ms(list[i+1].qpc-prev));
                        uint age=events.Length>0?events.Max(o=>o.queueAgeMs):0;
                        physicalSummaries.Add(new{phase=list[i].phase,kind=kind,count=events.Length,max_queue_age_ms=age,max_arrival_gap_ms=gap,
                            note="Arrival gap includes physical inactivity; continuous movement must be confirmed."});
                        if(age>=threshold) result=1;
                    }
                    continue; // Physical inactivity cannot certify latency or synthetic coverage.
                }
                foreach(string kind in new[]{"MOUSE","KEY_DOWN","KEY_UP"})
                {
                    int verdict; summaries.Add(Summarize(list[i].phase,list[i].qpc,list[i+1].qpc,kind,out verdict));
                    if(verdict==1) result=1; else if(verdict==2 && result==0) result=2;
                }
            }
            bool repeatedFailure; object[] episodes=Episodes(out repeatedFailure);
            if(repeatedFailure) result=1;
            if(list.Length<2 && result==0) result=2;
            using(var writer=new StreamWriter(output+".samples.csv",false,new UTF8Encoding(false)))
            {
                writer.WriteLine("id,kind,phase,sent_qpc,return_qpc,hook_qpc,window_qpc,expect_window,api_count,flags,event_time");
                foreach(Sample s in samples.Values.OrderBy(x=>x.id)) writer.WriteLine("{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10}",s.id,s.kind,s.phase,s.sent,s.returned,s.hook,s.received,s.expectWindow,s.apiCount,s.flags,s.eventTime);
            }
            using(var writer=new StreamWriter(output+".observations.csv",false,new UTF8Encoding(false)))
            {
                writer.WriteLine("kind,qpc,flags,event_time,queue_age_ms");
                foreach(var o in observations) writer.WriteLine("{0},{1},{2},{3},{4}",o.kind,o.qpc,o.flags,o.eventTime,o.queueAgeMs);
            }
            object summary=new{runId=runId,threshold_ms=threshold,frequency=Stopwatch.Frequency,markers=list,summaries=summaries,physical_summaries=physicalSummaries,
                slow_event_ms=slowMs,recovery_merge_ms=recoveryMs,slow_delivery_episodes=episodes,
                episode_note="Merged slow window-delivery intervals, not proof of uninterrupted physical input loss.",
                process_id=Process.GetCurrentProcess().Id,threads=threadInfo.ToArray(),heartbeat_stalls=heartbeatStalls.ToArray(),
                errors=errors.ToArray(),mouse_hook_handle=mouseHookHandle,key_hook_handle=keyHookHandle,mouse_hook_calls=mouseCalls,key_hook_calls=keyCalls,heartbeat_max_ms=maxBeat.Select(x=>Ms(x)).ToArray(),physical_input_note="Raw input is user-mode arrival, not hardware polling rate. No samples means N/A.",
                status=result==0?"PASS":result==1?"FAIL":"INCONCLUSIVE",exit_code=result};
            File.WriteAllText(output+".json",json.Serialize(summary),new UTF8Encoding(false));
            Console.WriteLine("RESULT {0} samples={1} errors={2} output={3}.json",result,samples.Count,errors.Count,output);
            return result;
        }
        static int SelfTest()
        {
            int n=0;
            Action<bool,string> check=(ok,label)=>{if(!ok)throw new Exception(label); n++;};
            check(Judge(0,0,0,0,true)==2,"empty");
            check(Judge(1,0,0,0,true)==2,"single");
            check(Judge(100,100,0,0,true)==2,"all lost");
            check(Judge(100,1,0,0,true)==2,"tail lost");
            check(Judge(100,0,499,20,true)==0,"499");
            check(Judge(100,0,500,20,true)==1,"500");
            check(Judge(100,0,600,20,true)==1,"cross boundary");
            check(Judge(100,0,1,600,true)==2,"focus/sender gap");
            check(Judge(100,0,1,20,false)==2,"registration/receiver invalid");
            check(Marshal.SizeOf(typeof(INPUT))==(IntPtr.Size==8?40:28),"INPUT layout");
            long unit=Stopwatch.Frequency/1000;
            check(MergeIntervals(new Interval[0],100).Count==0,"empty episodes");
            var merged=MergeIntervals(new[]{
                new Interval{begin=0,end=300*unit,count=1},
                new Interval{begin=350*unit,end=650*unit,count=1}},100);
            check(merged.Count==1 && merged[0].count==2 && Ms(merged[0].end)==650,"repeated subthreshold events");
            check(MergeIntervals(new[]{
                new Interval{begin=0,end=300*unit,count=1},
                new Interval{begin=401*unit,end=701*unit,count=1}},100).Count==2,"recovery separates episodes");
            check(MergeIntervals(new[]{
                new Interval{begin=0,end=600*unit,count=1},
                new Interval{begin=100*unit,end=200*unit,count=1}},0)[0].end==600*unit,"nested intervals");
            Console.WriteLine("SELFTEST PASS count="+n); return 0;
        }
        [STAThread] static int Main(string[] args)
        {
            CultureInfo.DefaultThreadCurrentCulture=CultureInfo.InvariantCulture;
            if(args.Length==1 && args[0]=="--self-test") return SelfTest();
            if(args.Length<3) { Console.Error.WriteLine("Usage: MouseRateSampler pipe output-prefix run-id [threshold] [physical] [max-seconds]"); return 2; }
            pipeName=args[0]; output=args[1]; runId=args[2];
            if(args.Length>3) threshold=int.Parse(args[3]);
            physicalOnly=args.Length>4 && args[4]=="physical";
            if(args.Length>5) maxSeconds=int.Parse(args[5]);
            string env=Path.Combine(Path.GetDirectoryName(Path.GetDirectoryName(Path.GetDirectoryName(typeof(Program).Assembly.Location))),".dbg","eide-freeze-crash.env");
            if(File.Exists(env)) foreach(string line in File.ReadAllLines(env)) if(line.StartsWith("DEBUG_SERVER_URL=")) debugUrl=line.Substring(17);
            start=Now();
            threadInfo.Enqueue(new ThreadInfo{role="supervisor",tid=GetCurrentThreadId()});
            Application.EnableVisualStyles();
            Thread reporting=StartThread(DebugLoop,false);
            Thread raw=StartThread(RawLoop,true), window=StartThread(ReceiverLoop,true);
            if(!receiverReady.WaitOne(10000)||!rawReady.WaitOne(10000)||!errors.IsEmpty)
            {
                Fail("Observer initialization incomplete"); stopping=true; return Save();
            }
            Thread hooks=StartThread(HookLoop,true);
            if(!hookReady.WaitOne(10000)||!errors.IsEmpty) { Fail("Hook initialization incomplete"); stopping=true; return Save(); }
            Thread sender=StartThread(SenderLoop,false); StartThread(PipeLoop,false);
            Console.WriteLine("READY pipe="+pipeName);
            while(!stopping)
            {
                long now=Now(); long[] beats={Interlocked.Read(ref receiverBeat),Interlocked.Read(ref hookBeat),Interlocked.Read(ref rawBeat),Interlocked.Read(ref senderBeat)};
                ObserveBeats(now,beats);
                if(Ms(now-start)>maxSeconds*1000) { Fail("Safety deadline reached without STOP"); stopping=true; }
                Thread.Sleep(10);
            }
            if(!sender.Join(3000)) Fail("Sender did not stop; pending input");
            // Drain queued messages before shutting down observers.
            if(!raw.Join(3000)) Fail("Raw observer did not stop");
            if(!hooks.Join(3000)) Fail("Hook observer did not stop");
            if(!window.Join(3000)) Fail("Receiver did not stop");
            if(!reporting.Join(3000)) Fail("Reporter did not stop");
            return Save();
        }
    }
}
