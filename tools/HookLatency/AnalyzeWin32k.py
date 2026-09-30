"""Offline ETW CSV analysis; never opens or changes another process."""
import argparse
import collections
import csv
import json
from pathlib import Path


def analyze(path, target_pid):
    pending = {}
    owners = {}
    queued = collections.Counter()
    routes = collections.Counter()
    received = collections.Counter()
    source_queues = collections.Counter()
    max_route_delay = {}
    hold_totals = collections.Counter()
    waits = []
    holds = []
    recycled = 0
    target = f"({target_pid})"
    with path.open(encoding="utf-8-sig", errors="replace", newline="") as stream:
        for row in csv.reader(stream, skipinitialspace=True):
            if len(row) < 12 or not row[0].startswith("Microsoft-Windows-Win32k/"):
                continue
            try:
                time = int(row[1].strip())
            except ValueError:
                continue
            name, process, tid = row[0], row[2].strip(), row[3].strip()
            if "/QueueEventMessage/" in name and len(row) >= 14:
                # QueueEventMessage may omit pti; retrieval supplies it later.
                key = (row[10].strip(), row[12].strip())
                if key in pending:
                    recycled += 1
                pending[key] = (process, tid, time)
                source_queues[process] += 1
                if target in process:
                    queued[key[1]] += 1
            elif "/RetrieveQueueEventMessage/" in name and len(row) >= 14:
                key = (row[10].strip(), row[12].strip())
                pti = row[11].strip()
                owners[pti] = f"{process} tid={tid}"
                received[(process, tid, key[1])] += 1
                origin = pending.pop(key, None)
                if origin and target in origin[0]:
                    route = (pti, key[1])
                    routes[route] += 1
                    max_route_delay[route] = max(
                        max_route_delay.get(route, 0), (time - origin[2]) / 1000
                    )
            elif "/ReleaseUserCrit/" in name:
                duration_ms = int(row[9]) / 10000
                # Counts use the machine QPC frequency (10 MHz in these traces).
                hold_totals[(process, tid)] += int(row[9])
                if duration_ms >= 1:
                    holds.append(dict(process=process, tid=tid, end_us=time,
                                      duration_ms=duration_ms, token=row[11].strip()))
            elif "/ExclusiveUserCrit/" in name or "/SharedUserCrit/" in name:
                duration_us = int(row[10])
                if duration_us >= 1000:
                    waits.append(dict(process=process, tid=tid, end_us=time,
                                      duration_ms=duration_us / 1000,
                                      token=row[11].strip()))
    pending_target = collections.Counter(
        key[1] for key, value in pending.items() if target in value[0]
    )
    return dict(
        source=str(path), target_pid=target_pid,
        note="Queue events are internal notifications, not all physical input. "
             "Missing retrieval may lie outside the exported time range. "
             "Pointer reuse and callback nesting can limit pairing.",
        overwritten_queue_keys=recycled,
        queue_sources=source_queues.most_common(20),
        target_queued_by_event=dict(queued),
        target_pending_by_event=dict(pending_target),
        target_routes=[
            dict(pti=key[0], event=key[1], receiver=owners.get(key[0], "UNOBSERVED"),
                 paired=count,
                 max_paired_delay_ms=max_route_delay.get(key))
            for key, count in routes.most_common()
        ],
        receivers=[dict(process=k[0], tid=k[1], event=k[2], count=n)
                   for k, n in received.most_common(25)],
        largest_holds=sorted(holds, key=lambda x: x["duration_ms"], reverse=True)[:30],
        largest_waits=sorted(waits, key=lambda x: x["duration_ms"], reverse=True)[:30],
        hold_qpc_totals=[dict(process=k[0], tid=k[1], ticks=n)
                         for k, n in hold_totals.most_common(20)],
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("target_pid", type=int)
    args = parser.parse_args()
    result = analyze(args.csv, args.target_pid)
    args.csv.with_suffix(".analysis.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )
