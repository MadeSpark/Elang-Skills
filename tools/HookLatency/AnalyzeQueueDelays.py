"""Per-receiver queue-delay distribution + backlog-over-time analysis.

Reads the Win32k ETW CSV exported earlier and computes, for every
QueueEventMessage -> RetrieveQueueEventMessage pair whose origin is the
target PID:
  - enqueue timestamp
  - dequeue (retrieval) timestamp
  - delay = retrieve - queue
Aggregates per receiving route (pti) into p50/p90/p99/max, plus a
queue-backlog time series (bucketed) so we can see whether any receiver
is a slow consumer backing the notification queue up.

Read-only: writes one JSON next to the CSV.
"""
import argparse
import bisect
import collections
import json
from pathlib import Path


def pct(sorted_vals, p):
    if not sorted_vals:
        return None
    idx = min(len(sorted_vals) - 1, max(0, int(round((p / 100.0) * (len(sorted_vals) - 1)))))
    return sorted_vals[idx]


def analyze(path, target_pid, bucket_ms=200.0):
    pending = {}
    owners = {}
    routes = collections.defaultdict(list)  # (pti,event) -> [delay_ms]
    enq_times = collections.defaultdict(list)  # route -> [enqueue_us]
    ret_times = collections.defaultdict(list)  # route -> [retrieve_us]
    target_enq = []  # (time_us, key) for target-origin queues
    target = f"({target_pid})"
    t_min = None
    t_max = None
    with path.open(encoding="utf-8-sig", errors="replace", newline="") as stream:
        import csv
        for row in csv.reader(stream, skipinitialspace=True):
            if len(row) < 12 or not row[0].startswith("Microsoft-Windows-Win32k/"):
                continue
            try:
                time = int(row[1].strip())
            except ValueError:
                continue
            if t_min is None:
                t_min = time
            t_max = time
            name, process, tid = row[0], row[2].strip(), row[3].strip()
            if "/QueueEventMessage/" in name and len(row) >= 14:
                key = (row[10].strip(), row[12].strip())
                if target in process:
                    target_enq.append((time, key))
                pending[key] = (process, tid, time)
            elif "/RetrieveQueueEventMessage/" in name and len(row) >= 14:
                key = (row[10].strip(), row[12].strip())
                pti = row[11].strip()
                owners[pti] = f"{process} tid={tid}"
                origin = pending.pop(key, None)
                if origin and target in origin[0]:
                    delay_ms = (time - origin[2]) / 1000.0
                    route = (pti, key[1])
                    routes[route].append(delay_ms)
                    enq_times[route].append(origin[2])
                    ret_times[route].append(time)

    t0 = t_min or 0
    routes_out = []
    for route, delays in sorted(routes.items(), key=lambda kv: -len(kv[1])):
        d = sorted(delays)
        enq = sorted(enq_times[route])
        ret = sorted(ret_times[route])
        # backlog over time: walk merged enqueue/retrieve markers
        backlog_peak = 0
        cur = 0
        marks = [(t, 1) for t in enq] + [(t, -1) for t in ret]
        marks.sort()
        backlog_curve = []
        for t, delta in marks:
            cur += delta
            backlog_peak = max(backlog_peak, cur)
            backlog_curve.append((t, cur))
        # bucket the backlog curve for compact output
        bucketed = []
        if marks:
            cur_b = 0
            i = 0
            t_end = marks[-1][0]
            b = t0
            while b <= t_end:
                nb = b + bucket_ms * 1000.0
                peak_in_bucket = 0
                while i < len(marks) and marks[i][0] < nb:
                    cur_b += marks[i][1]
                    peak_in_bucket = max(peak_in_bucket, cur_b)
                    i += 1
                bucketed.append(peak_in_bucket)
                b = nb
        routes_out.append(dict(
            pti=route[0], event=route[1],
            receiver=owners.get(route[0], "UNOBSERVED"),
            paired=len(delays),
            delay_ms=dict(
                p50=round(pct(d, 50) or 0, 3), p90=round(pct(d, 90) or 0, 3),
                p99=round(pct(d, 99) or 0, 3), max=round(d[-1] if d else 0, 3)),
            backlog=dict(peak=backlog_peak, bucket_ms=bucket_ms, buckets=bucketed),
            first_enq_us=enq[0] - t0 if enq else None,
            last_ret_us=ret[-1] - t0 if ret else None,
        ))

    # target enqueue histogram (all target-origin notifications, per bucket)
    hist = collections.Counter()
    for t, _ in target_enq:
        hist[int((t - t0) / (bucket_ms * 1000.0))] += 1
    return dict(
        source=str(path), target_pid=target_pid,
        trace_span_ms=round((t_max - t_min) / 1000.0, 1) if t_min is not None else None,
        bucket_ms=bucket_ms,
        target_enqueue_total=len(target_enq),
        enqueue_histogram=[hist[k] for k in sorted(hist)],
        routes=routes_out,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("target_pid", type=int)
    parser.add_argument("--bucket-ms", type=float, default=200.0)
    args = parser.parse_args()
    result = analyze(args.csv, args.target_pid, args.bucket_ms)
    out = args.csv.with_name(args.csv.stem + ".delays.json")
    out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2)[:4000])
