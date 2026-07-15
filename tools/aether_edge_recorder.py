#!/usr/bin/env python3
"""aether_edge_recorder.py — AetherSDR session log -> edge events ->
(a) self-contained SVG lane view (HTML) and (b) Perfetto trace JSON.

Parses one or more AetherSDR session logs (~/.config/AetherSDR/logs/ on Linux,
~/Library/Preferences/AetherSDR/logs/ on macOS; format `[HH:MM:SS.mmm] LVL
category: message`, continuation lines indented) into a lane/edge model:

  TX keyed (TCI trx)   spans between `TCI: trx request ... txOn= true/false`
  TX_CHRONO pacing     spans between `TCI: TX_CHRONO started` / `... stopped`
  TX summary           instants carrying every key=value from `TCI TX summary`
  main-thread stalls   spans from `MainThreadWatchdog: event loop stalled ~N ms`
  WRN/ERR/CRT          instants for every warning-or-worse line, any category
  <category> firehose  one instant per log line, one lane per category (--no-firehose)
  counters             `--counter fwdPower` etc: numeric `key= value` -> value trace

and renders it as:
  --json out.json   Chrome trace-event JSON -> open at https://ui.perfetto.dev
  --html out.html   dark scope-style SVG lanes, wheel to zoom, drag to pan,
                    hover an edge for the raw log line

Usage:
  python3 aether_edge_recorder.py SESSION.LOG [MORE.LOG ...]
      [--json out.json] [--html out.html] [--counter KEY ...]
      [--no-firehose] [--date YYYYMMDD]

Timestamps carry no date; it comes from the filename (aethersdr-YYYYMMDD-*.log)
or --date, and a backwards jump of >12 h is treated as midnight rollover.
Evidence-rule note: this is post-processing of the stock build's built-in
category logging — no app instrumentation involved.
"""

import argparse
import html
import json
import os
import re
import sys
from datetime import datetime, timedelta

LINE_RE = re.compile(r"^\[(\d{2}):(\d{2}):(\d{2})\.(\d{3})\] (\w{3}) ([\w.]+): (.*)$")
FNAME_DATE_RE = re.compile(r"(\d{8})-\d{6}")
# qDebug() inserts a space after `key=`; nospace streams don't. Accept both,
# with optional quotes around the value.
KV_RE = re.compile(r'([A-Za-z_][\w.]*)=\s*"?([^\s"]+)"?')
STALL_RE = re.compile(r"event loop stalled\D*(\d+)\s*ms", re.IGNORECASE)
WIRE_TRX_RE = re.compile(r'"?trx:(\d+),(true|false)')
XMIT_RE = re.compile(r'\|xmit (0|1)"')

LEVELS = {"DBG": 0, "INF": 1, "WRN": 2, "ERR": 3, "CRT": 4}


class Lane:
    def __init__(self, name, kind, order):
        self.name = name
        self.kind = kind          # "span" | "instant" | "counter"
        self.order = order
        self.spans = []           # [t0_us, t1_us|None, label, detail]
        self.instants = []        # [t_us, label, detail]
        self.points = []          # [t_us, value]  (counter)


class Model:
    def __init__(self):
        self.lanes = {}
        self.stats = {}
        self.t0 = None
        self.t1 = None

    def lane(self, key, name, kind, order):
        if key not in self.lanes:
            self.lanes[key] = Lane(name, kind, order)
        return self.lanes[key]

    def count(self, key):
        self.stats[key] = self.stats.get(key, 0) + 1

    def see(self, ts):
        self.t0 = ts if self.t0 is None else min(self.t0, ts)
        self.t1 = ts if self.t1 is None else max(self.t1, ts)


def parse_log(path, base_date):
    """Yield (ts_us, level, category, message) with continuations folded in."""
    if base_date is None:
        m = FNAME_DATE_RE.search(os.path.basename(path))
        base_date = m.group(1) if m else "19700101"
    day = datetime.strptime(base_date, "%Y%m%d")
    prev_ts = None
    cur = None
    with open(path, errors="replace") as f:
        for raw in f:
            m = LINE_RE.match(raw.rstrip("\n"))
            if not m:
                if cur is not None:  # continuation line
                    cur[3] += " | " + raw.strip()
                continue
            if cur is not None:
                yield tuple(cur)
            h, mi, s, ms, lvl, cat, msg = m.groups()
            t = day + timedelta(hours=int(h), minutes=int(mi),
                                seconds=int(s), milliseconds=int(ms))
            if prev_ts is not None and (prev_ts - t) > timedelta(hours=12):
                day += timedelta(days=1)  # midnight rollover
                t += timedelta(days=1)
            prev_ts = t
            cur = [int(t.timestamp() * 1_000_000), lvl, cat, msg]
    if cur is not None:
        yield tuple(cur)


def kv_args(msg):
    return {k: v for k, v in KV_RE.findall(msg)}


def build_model(paths, base_date, counters, firehose):
    md = Model()
    fh_order = 100
    for path in paths:
        for ts, lvl, cat, msg in parse_log(path, base_date):
            md.see(ts)
            # --- wire-level extractors (work on DBG captures of any vintage) --
            wm = WIRE_TRX_RE.search(msg)
            if wm and "TCI rx" in msg:
                ln = md.lane("wtrx", "WSJT-X trx (TCI wire)", "span", 0)
                if wm.group(2) == "true":
                    ln.spans.append([ts, None, "trx:%s,true" % wm.group(1), msg])
                    md.count("wire_keyup")
                else:
                    for sp in reversed(ln.spans):
                        if sp[1] is None:
                            sp[1] = ts
                            break
                    md.count("wire_unkey")
            xm = XMIT_RE.search(msg)
            if xm:
                ln = md.lane("xmit", "radio keyed (xmit)", "span", 0.5)
                if xm.group(1) == "1":
                    if not (ln.spans and ln.spans[-1][1] is None):
                        ln.spans.append([ts, None, "xmit 1", msg])
                        md.count("xmit_on")
                else:
                    for sp in reversed(ln.spans):
                        if sp[1] is None:
                            sp[1] = ts
                            md.count("xmit_off")
                            break
            if "transmit set dax=" in msg:
                md.lane("dax", "transmit set dax=", "instant", 0.7) \
                  .instants.append([ts, msg[msg.find("transmit set"):][:40], msg])
                md.count("dax_set")

            if "TCI: trx request" in msg:
                a = kv_args(msg)
                ln = md.lane("keyed", "TX keyed (TCI trx)", "span", 1)
                if a.get("txOn") == "true":
                    ln.spans.append([ts, None,
                                     "TX trx=%s %s" % (a.get("trx", "?"),
                                                       a.get("route", "")), msg])
                    md.count("keyup")
                else:
                    for sp in reversed(ln.spans):
                        if sp[1] is None:
                            sp[1] = ts
                            break
                    md.count("unkey")
            elif "TX_CHRONO started" in msg:
                md.lane("chrono", "TX_CHRONO pacing", "span", 2) \
                  .spans.append([ts, None, "TX_CHRONO", msg])
                md.count("chrono_start")
            elif "TX_CHRONO stopped" in msg:
                ln = md.lane("chrono", "TX_CHRONO pacing", "span", 2)
                for sp in reversed(ln.spans):
                    if sp[1] is None:
                        sp[1] = ts
                        break
                md.count("chrono_stop")
            elif "TCI TX summary" in msg:
                a = kv_args(msg)
                md.lane("summary", "TX summary", "instant", 3) \
                  .instants.append([ts, "TX summary %s" % a.get("reason", ""), msg])
                md.count("tx_summary")
            elif "event loop stalled" in msg:
                sm = STALL_RE.search(msg)
                dur = int(sm.group(1)) * 1000 if sm else 1_000_000
                md.lane("stalls", "main-thread stalls", "span", 4) \
                  .spans.append([ts - dur, ts, "stall", msg])
                md.count("stall")

            if LEVELS.get(lvl, 0) >= LEVELS["WRN"]:
                md.lane("warn", "WRN/ERR/CRT", "instant", 5) \
                  .instants.append([ts, "%s %s" % (lvl, cat), msg])
                md.count("warnings")

            for key in counters:
                if key + "=" in msg:
                    try:
                        val = float(kv_args(msg)[key])
                    except (KeyError, ValueError):
                        continue
                    md.lane("ctr_" + key, key, "counter", 50) \
                      .points.append([ts, val])
                    md.count("counter_" + key)

            if firehose:
                k = "fh_" + cat
                if k not in md.lanes:
                    fh_order += 1
                md.lane(k, cat, "instant", fh_order) \
                  .instants.append([ts, msg[:80], msg])
                md.count("firehose")

    last = md.t1 or 0
    for ln in md.lanes.values():
        for sp in ln.spans:
            if sp[1] is None:
                print("note: unclosed span %r on %r — closing at last ts"
                      % (sp[2], ln.name), file=sys.stderr)
                sp[1] = last

    # Counter gap-fill: the trace-event counter model has no "no data" notion —
    # viewers step-hold the last sample, so a stalled meter stream reads as a
    # steady value (e.g. the radio's tx_sensors stream dies mid-ramp at unkey
    # and Perfetto shows ~30 W across the whole RX gap). Insert synthetic
    # floor-value samples (min observed = the meter floor) at gap boundaries
    # so held lines drop instead of lying; they carry a synthetic=true arg.
    for ln in md.lanes.values():
        if ln.kind != "counter" or len(ln.points) < 2:
            continue
        floor = min(p[1] for p in ln.points)
        filled = []
        for i, p in enumerate(ln.points):
            filled.append(p)
            if i + 1 < len(ln.points) and ln.points[i + 1][0] - p[0] > 2_000_000:
                filled.append([p[0] + 250_000, floor, True])
                filled.append([ln.points[i + 1][0] - 250_000, floor, True])
        filled.append([ln.points[-1][0] + 250_000, floor, True])
        ln.points = filled

    # Counter coverage lanes: explicit spans over the regions where samples
    # actually exist (gap threshold 2 s) — the ground truth for what above is
    # measured vs synthetic fill.
    for key, ln in list(md.lanes.items()):
        if ln.kind != "counter" or not ln.points:
            continue
        real = [p for p in ln.points if len(p) < 3 or not p[2]]
        if not real:
            continue
        cov = md.lane(key + "_cov", ln.name + " [samples present]", "span",
                      ln.order + 0.1)
        t0 = prev = real[0][0]
        n = 1
        for t, _v in ((p[0], p[1]) for p in real[1:]):
            if t - prev > 2_000_000:
                cov.spans.append([t0, prev, "%d samples" % n,
                                  "meter stream active; step-hold beyond this is an artifact"])
                t0, n = t, 0
            prev = t
            n += 1
        cov.spans.append([t0, prev, "%d samples" % n,
                          "meter stream active; step-hold beyond this is an artifact"])
    return md


# ---------------------------------------------------------------- Perfetto --
def emit_perfetto(md, out):
    ev, meta = [], []
    meta.append({"ph": "M", "pid": 1, "name": "process_name",
                 "args": {"name": "AetherSDR session"}})
    for tid, (key, ln) in enumerate(
            sorted(md.lanes.items(), key=lambda kv: kv[1].order), start=1):
        meta.append({"ph": "M", "pid": 1, "tid": tid, "name": "thread_name",
                     "args": {"name": ln.name}})
        meta.append({"ph": "M", "pid": 1, "tid": tid,
                     "name": "thread_sort_index", "args": {"sort_index": ln.order}})
        for t0, t1, label, detail in ln.spans:
            ev.append({"ph": "X", "pid": 1, "tid": tid, "ts": t0,
                       "dur": max(t1 - t0, 1), "name": label,
                       "args": {"log": detail[:400]}})
        for t, label, detail in ln.instants:
            ev.append({"ph": "i", "s": "t", "pid": 1, "tid": tid, "ts": t,
                       "name": label, "args": {"log": detail[:400]}})
        for p in ln.points:
            ev.append({"ph": "C", "pid": 1, "ts": p[0], "name": ln.name,
                       "args": {ln.name: p[1]}})
    with open(out, "w") as f:
        json.dump({"traceEvents": meta + ev, "displayTimeUnit": "ms"}, f)
    return len(ev)


# --------------------------------------------------------------------- SVG --
SVG_COLORS = ["#C4842A", "#3F9FD0", "#9C77DD", "#33AF55"]

HTML_SHELL = """<!doctype html><meta charset="utf-8">
<title>%(title)s</title>
<style>
 body{margin:0;background:#0F1512;color:#9fb0a7;
      font:12px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
 header{padding:10px 16px;border-bottom:1px solid #1e2a24}
 header b{color:#d5ded9}
 #hint{color:#5c6b63}
 svg{display:block;width:100%%;height:%(height)dpx;cursor:grab}
 svg:active{cursor:grabbing}
 #tip{position:fixed;display:none;max-width:520px;background:#d5ded9;color:#0F1512;
      padding:6px 8px;border-radius:3px;font-size:11px;line-height:1.4;
      pointer-events:none;z-index:9;word-break:break-all}
</style>
<header><b>%(title)s</b> · %(t0)s → %(t1)s · %(nlanes)d lanes
 <span id="hint">· wheel = zoom · drag = pan · hover = raw log line</span></header>
<svg id="s" viewBox="0 0 1600 %(height)d" preserveAspectRatio="none"></svg>
<div id="tip"></div>
<script>
var LANES=%(lanes)s,T0=%(vt0)d,T1=%(vt1)d,W=1600,LX=210,ROWH=34,PAD=26;
var svg=document.getElementById("s"),tip=document.getElementById("tip");
var NS="http://www.w3.org/2000/svg",view={t0:T0,t1:T1};
function el(p,n,a,t){var e=document.createElementNS(NS,n);
 for(var k in a)e.setAttribute(k,a[k]);if(t)e.textContent=t;p.appendChild(e);return e}
function tx(t){return LX+(t-view.t0)/(view.t1-view.t0)*(W-LX-10)}
function fmt(us){var d=new Date(us/1000);
 return ("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"
 +("0"+d.getSeconds()).slice(-2)+"."+("00"+d.getMilliseconds()).slice(-3)}
function hook(n,html){n.addEventListener("mousemove",function(ev){
 tip.innerHTML=html;tip.style.display="block";
 tip.style.left=Math.min(ev.clientX+14,innerWidth-540)+"px";
 tip.style.top=(ev.clientY+14)+"px"});
 n.addEventListener("mouseleave",function(){tip.style.display="none"})}
function render(){
 svg.textContent="";
 var span=view.t1-view.t0,step=Math.pow(10,Math.ceil(Math.log10(span/8)));
 if(span/step<4)step/=2;
 for(var t=Math.ceil(view.t0/step)*step;t<view.t1;t+=step){
  var x=tx(t);
  el(svg,"line",{x1:x,y1:PAD-8,x2:x,y2:PAD+LANES.length*ROWH,
   stroke:"#1e2a24","stroke-width":1});
  el(svg,"text",{x:x+3,y:PAD-12,fill:"#5c6b63","font-size":10,
   "font-family":"ui-monospace,Menlo,monospace"},fmt(t))}
 LANES.forEach(function(l,i){
  var y=PAD+i*ROWH+ROWH/2,c=l.color;
  el(svg,"text",{x:8,y:y+4,fill:"#9fb0a7","font-size":11,
   "font-family":"ui-monospace,Menlo,monospace"},
   l.name.length>28?l.name.slice(0,27)+"…":l.name);
  el(svg,"line",{x1:LX,y1:y,x2:W-10,y2:y,stroke:"#1e2a24","stroke-width":1});
  (l.spans||[]).forEach(function(s){
   if(s[1]<view.t0||s[0]>view.t1)return;
   var x0=Math.max(tx(s[0]),LX),x1=Math.min(tx(s[1]),W-10);
   var r=el(svg,"rect",{x:x0,y:y-9,width:Math.max(x1-x0,2),height:18,rx:2,
    fill:c,opacity:0.75});
   hook(r,"<b>"+s[2]+"</b><br>"+fmt(s[0])+" → "+fmt(s[1])+" ("
    +((s[1]-s[0])/1000).toFixed(1)+" ms)<br>"+s[3])});
  (l.instants||[]).forEach(function(s){
   if(s[0]<view.t0||s[0]>view.t1)return;
   var x=tx(s[0]);
   el(svg,"line",{x1:x,y1:y-9,x2:x,y2:y+9,stroke:c,"stroke-width":1.6});
   var h=el(svg,"rect",{x:x-4,y:y-11,width:8,height:22,fill:"transparent"});
   hook(h,"<b>"+s[1]+"</b><br>"+fmt(s[0])+"<br>"+s[2])});
  if(l.points&&l.points.length){
   var vmax=l.points.reduce(function(m,p){return Math.max(m,p[1])},1),d="";
   l.points.forEach(function(p,j){
    d+=(j?" L":"M")+tx(p[0])+","+(y+9-18*(p[1]/vmax))});
   el(svg,"path",{d:d,stroke:c,fill:"none","stroke-width":1.6});
   el(svg,"text",{x:W-95,y:y-10,fill:c,"font-size":9},"max "+vmax)}
 })}
svg.addEventListener("wheel",function(ev){ev.preventDefault();
 var f=ev.deltaY>0?1.25:0.8,r=svg.getBoundingClientRect();
 var px=(ev.clientX-r.left)/r.width*W;
 var t=view.t0+(px-LX)/(W-LX-10)*(view.t1-view.t0);
 view.t0=Math.max(T0,t-(t-view.t0)*f);view.t1=Math.min(T1,t+(view.t1-t)*f);
 render()},{passive:false});
var drag=null;
svg.addEventListener("mousedown",function(ev){drag={x:ev.clientX,t0:view.t0,t1:view.t1}});
addEventListener("mousemove",function(ev){if(!drag)return;
 var r=svg.getBoundingClientRect();
 var dt=(drag.x-ev.clientX)/r.width*W/(W-LX-10)*(drag.t1-drag.t0);
 if(drag.t0+dt<T0)dt=T0-drag.t0;if(drag.t1+dt>T1)dt=T1-drag.t1;
 view.t0=drag.t0+dt;view.t1=drag.t1+dt;render()});
addEventListener("mouseup",function(){drag=null});
render();
</script>"""


def emit_html(md, out, title):
    lanes = []
    ordered = sorted(md.lanes.values(), key=lambda l: l.order)
    for i, ln in enumerate(ordered):
        lanes.append({
            "name": ln.name,
            "color": SVG_COLORS[i % len(SVG_COLORS)],
            "spans": [[s[0], s[1], html.escape(s[2]), html.escape(s[3][:400])]
                      for s in ln.spans],
            "instants": [[s[0], html.escape(s[1]), html.escape(s[2][:400])]
                         for s in ln.instants],
            "points": ln.points,
        })
    height = 26 + len(lanes) * 34 + 20
    doc = HTML_SHELL % {
        "title": html.escape(title), "height": height,
        "t0": datetime.fromtimestamp(md.t0 / 1e6).strftime("%H:%M:%S"),
        "t1": datetime.fromtimestamp(md.t1 / 1e6).strftime("%H:%M:%S"),
        "nlanes": len(lanes),
        "lanes": json.dumps(lanes), "vt0": md.t0, "vt1": md.t1,
    }
    with open(out, "w") as f:
        f.write(doc)


def main():
    ap = argparse.ArgumentParser(
        description="AetherSDR session log -> SVG lane view + Perfetto JSON")
    ap.add_argument("logs", nargs="+", help="session log file(s), merged on one clock")
    ap.add_argument("--json", help="write Perfetto trace JSON here")
    ap.add_argument("--html", help="write the SVG lane view (self-contained HTML) here")
    ap.add_argument("--counter", action="append", default=[],
                    help="numeric key=value to trace (repeatable, e.g. --counter fwdPower)")
    ap.add_argument("--no-firehose", action="store_true",
                    help="skip the one-instant-per-line per-category lanes")
    ap.add_argument("--date", help="YYYYMMDD base date if not in the filename")
    args = ap.parse_args()
    if not args.json and not args.html:
        args.json = "aether-trace.json"
        args.html = "aether-trace.html"

    md = build_model(args.logs, args.date, args.counter, not args.no_firehose)
    if md.t0 is None:
        sys.exit("no parsable log lines found")
    if args.json:
        n = emit_perfetto(md, args.json)
        print("wrote %s: %d events -> open at https://ui.perfetto.dev" % (args.json, n))
    if args.html:
        emit_html(md, args.html, "AetherSDR edges — " +
                  ", ".join(os.path.basename(p) for p in args.logs))
        print("wrote %s (self-contained; open in a browser)" % args.html)
    for k in sorted(md.stats):
        print("  %-16s %d" % (k, md.stats[k]))


if __name__ == "__main__":
    main()
