# AetherSDR — RX Audio Path & the AF Menu (reference)

Personal reference for how RX audio flows in AetherSDR, what each AF-menu control
actually touches, and how to set it up for **WSJT-X over TCI**. Written 2026-07-01
against v26.6.5 (`f99842e8`). Provenance = code, cited inline; if line numbers drift,
grep the symbol.

> TL;DR — **What WSJT-X hears is the radio's DAX stream, not your speaker audio.**
> So: **SQL must be OFF**, and **AGC affects decode** (radio-side). **AF volume / NR /
> speaker-mute are speaker-only and do NOT touch WSJT-X.**
>
> ⚠️ **OPEN QUESTION (under investigation):** whether **PC Audio** must be *enabled* for TCI
> audio. The #1071 code comment says the DAX/TCI stream isn't auto-created and users must enable
> PC Audio manually — **but field observation is that toggling PC Audio off only mutes the
> speaker while WSJT-X keeps decoding.** So PC Audio may behave as a mute (once the stream
> exists), not a hard prerequisite. There's also a confusing **per-band** effect (audio works on
> some bands, not others; fiddling AF/AGC "fixes" it) whose mechanism is not yet pinned down.
> **Test before trusting either explanation** (see the test protocol at the bottom).

---

## The mental model

The Flex radio does the *real* per-slice DSP (filters, **AGC**, **squelch**), then emits
several **independent** streams. AetherSDR is a client that routes them to different sinks:

```
  FLEX RADIO — per-slice RX DSP: filter, AGC, SQUELCH  (all RADIO-SIDE)
        │
        ├── DAX audio stream  ─────────────►  DAX virtual audio device ──► any app using DAX
        │      (post AGC/SQL; radio-side)            │
        │                                            └─► TciServer.onDaxAudioReady ──► WSJT-X (TCI)
        │
        ├── panadapter / waterfall (calibrated dBm) ──► spectrum display
        │
        └── slice "PC Audio" stream ──► AudioEngine ──► [NR2, AF volume, mute] ──► your speakers
                                         (CLIENT-side DSP; SPEAKER PATH ONLY)
```

**The decisive fact:** TCI audio is sourced *exclusively from DAX*, not from the speaker
path — deliberately, so muting your speakers doesn't kill the decode feed.

> `src/gui/MainWindow_Session.cpp:1471` —
> *"TCI audio feeds exclusively from DAX (not audioDataReady) so that audio_mute doesn't
> kill TCI audio (#1331)."*
> Wiring: `PanadapterStream::daxAudioReady → TciServer::onDaxAudioReady`
> (`MainWindow_Session.cpp:1474`).

---

## Where each control acts

| Control | Scope | Affects WSJT-X? | Notes |
|---|---|---|---|
| **AF** (volume slider) | **Client / speaker** | **No** | "Audio output volume for this slice" (`RxApplet.cpp:1155`). Your ears only. Do **not** confuse with "PC Audio". |
| **PC Audio** toggle | **⚠️ UNCERTAIN** | **⚠️ unconfirmed** | Code (#1071, `MainWindow_Session.cpp:1482`) implies enabling it is required to create the DAX/TCI stream; **field observation is it only mutes the speaker while WSJT-X keeps decoding.** Needs the test protocol below to resolve. |
| **Speaker mute** (audio_mute) | **Client / speaker** | **No** | Once the stream is running, muting speakers does NOT stop the TCI feed (#1331). Consistent with the field observation above. |
| **NR / NR2 / NB** | **Client / speaker** | **No** | Applied on the presentation/speaker path, not on DAX. Safe to run heavy NR without hurting decodes. |
| **AGC** (Off/Slow/Med/Fast) + **AGC-T** | **Radio (slice attr)** | **Yes** | `SliceModel::setAgcMode` (`RxApplet.cpp:987`), pushed to the radio. Shapes the DAX audio → shapes decode. |
| **SQL** (squelch) | **Radio (slice attr)** | **Yes** | Gates the slice's audio output *including DAX/TCI*. If it engages, WSJT-X gets **silence** during quiet periods → missed decodes. |
| **DAX / TCI RX level** | Stream gain | **Yes** | `TciServer::setRxChannelGain`. This is the knob that sets how hot WSJT-X's input is. |

**Three sinks, not competing settings:**
- **PC Audio** = your local speakers (AF, NR, mute apply).
- **DAX** = virtual audio device carrying the radio's slice audio.
- **TCI server** = the same DAX audio + CAT + spots, streamed over a websocket to TCI clients (WSJT-X).

The "AF menu feels confusing" reaction is legitimate: one panel mixes a **client** control
(AF), and two **radio** controls (AGC, SQL) that silently change what WSJT-X decodes.

---

## WSJT-X-over-TCI setup checklist

1. **Squelch OFF** on the slice. Non-negotiable for digital — squelch gates DAX/TCI audio.
3. **AGC** → **Off** (or a fixed AGC-T) for stable levels; avoid AGC pumping on strong signals.
4. **AF volume / speaker mute** → your ears only; irrelevant to decode once the stream is up.
5. **NR/NB** → your preference; they don't touch the decode feed.
6. **DAX/TCI RX level** → adjust so WSJT-X's input meter sits in range (not too hot — cf. #3479).
7. In WSJT-X: rig = TCI, audio via TCI (or the DAX device). Both ultimately pull the same
   radio DAX stream.

**Per-band gotcha:** switching bands loads a slice/profile, and profile loads have reset these
before (e.g. #3263 spurious `squelch=1`). If TCI audio works on one band but not another, check
that **PC Audio is still enabled** and **squelch is still off** on the failing band — the state
may not have carried over. (This is distinct from #3669, where DAX 1 fails to start on connect
even with PC Audio on, needing a mode change + DAX toggle to recover.)

---

## Level & AGC for FT8 — how it works + tests

**How the level chain works (code-verified):**
- The DAX/TCI feed applies **no AGC** — just a **flat per-channel gain** (`TciServer.cpp:1279`,
  `dst[i]=audioSrc[i]*gain`) and a defensive **±1.0 clip** (`:998`). No compression/normalization.
- The **only AGC** is the radio slice's AGC, applied on the Flex *before* the DAX stream exists.
- **Level knobs are per DAX channel (1-4), and there are TWO of them for the same channels**
  (`TciApplet.cpp:316`): **TCI RX gain** (TCI applet; only affects clients on *TCI audio*) and
  **DAX RX gain** (DAX applet; only affects the *DAX virtual device*). Both default 0.5, flat gain.
- **RF gain** (radio-side, per slice/antenna) sits upstream of everything → it's the coarse level
  and sets headroom against the clip. **AF** is speaker-only, not in this path.

**Recommended FT8 setup (matches what's been working):** radio AGC **off**, set level with
**antenna/slice RF gain**, then fine-trim with the *effective* DAX/TCI gain slider; keep WSJT-X's
input meter mid-scale (~30-50 dB) with headroom. This is sound — RF gain is the real level
control when AGC is off.

**Why the TCI slider may "do nothing":** if WSJT-X takes audio over the **DAX device** (not TCI
audio), the **TCI** RX gain slider is the wrong knob — use **DAX** RX gain. Or you're moving a
channel (RX1-4) your slice doesn't map to.

### Control tests (build your own mental model)
Prereq: enable **CAT + DAX** logging (Help → Support → Logging), restart, tail
`~/.config/AetherSDR/logs/aethersdr.log`. Watch WSJT-X's input dB bar as the reference meter.

- **T1 — which path is WSJT-X on?** Look at the `TCI: DAX RX audio … enabled_clients=… frames_sent=…`
  line. `enabled_clients ≥ 1` + `frames_sent` climbing → WSJT-X is on **TCI audio** (TCI slider is
  your knob). No such line but WSJT-X still decodes → it's on the **DAX device** (DAX slider is your
  knob; TCI slider will do nothing — the likely cause of your observation).
- **T2 — which channel?** The RX sliders are per-channel (RX1-4), mapped from your slice
  (`TciApplet.cpp:316`). Confirm the active channel; only that slider affects your audio.
- **T3 — prove the effective slider works.** On the correct path+channel, move the gain
  0.5 → 1.0 → 0.2. Expect the applet's own RX level meter (post-gain, `TciServer.cpp:1213`) to move
  ~+6 dB then ~-8 dB, and WSJT-X's bar to follow. Applet meter moves but WSJT-X doesn't → you're on
  the *other* path. Neither moves → wrong channel (or a bug worth reporting).
- **T4 — RF gain vs slider (headroom/clip).** With AGC off, sweep antenna RF gain and watch both
  meters: RF gain = coarse level; push it until WSJT-X pegs / distorts to feel the ±1.0 clip ceiling,
  then back off. The DAX/TCI slider is the fine trim below that.
- **T5 — AGC pumping demo (why off is better).** On a busy band with strong signals, briefly set
  AGC med/fast and watch the RX level meter jump when a strong signal keys; compare AGC off
  (steadier). That pumping is what costs weak-signal decodes.

## KiwiSDR caveats (different from the above)

- **Kiwi audio is speaker/monitor-only** — decoded Kiwi PCM is mixed into the AudioEngine
  speaker path (`AudioEngine.cpp:~1185`) and **never enters DAX**. So a Kiwi RX **cannot** feed
  WSJT-X today (capability request tracked on RFC #3894 `supportsRxAudioExport`; see #3950 for
  the related idle-disconnect bug). You *can* hear a Kiwi (e.g. monitor your own SSB), just not
  decode it via DAX/TCI.
- **Kiwi waterfall is uncalibrated and lower-res** — rows are raw display-intensity bytes,
  *not* calibrated dBm (unlike the Flex), and coarser; they're reprojected to the display via
  interpolation (`KiwiSdrTraceMath`). So a Kiwi waterfall will inherently look less crisp than
  the Flex — no control changes that.
- **The contrast/floor controls DO work on Kiwi** — the same waterfall overlay-menu sliders
  re-scope when a Kiwi is active (`SpectrumOverlayMenu.cpp:1908`): **Gain** = contrast (cell dB),
  **Black** = floor (−30…+30 dB, "KiwiSDR waterfall floor adjustment"), **Rate** = Kiwi rate,
  **Auto** = reset floor to automatic baseline. Workflow: hit **Auto** first, then nudge
  **Black** and **Gain** to set signal-vs-noise. (Not a bug — just separate, easy-to-miss
  controls; the earlier impression that they "don't map to Kiwi" was mistaken.)

---

## Related issues (as of 2026-07-01)

- **#3669** (open) — TCI audio (DAX 1) doesn't start on connect; needs mode change + DAX toggle.
- **#3366** (open) — TCI audio on Win11 not working (Windows).
- **#3730** (open) — Level/Compression meter wrong while using WSJT-X.
- **#2885** (open) — Excessive ALC on Slice D for WSJT-X digital (B/C fine).
- **#3595** (open) — WSJT-X spots via TCI paint on both slice waterfalls regardless of band.
- **#2392** (open) — TCI+WSJT-X slice freq drifts to TX offset after TX ("Fake it" split).
- **#3305** (open, refactor) — Centralize DAX RX stream ownership (multi-slice DAX/TCI hardening).
- Squelch-mis-engage (decode killers): **#3505**, **#2504**, **#2743**, **#3326**, **#3263** (closed).
- **#3479** (closed) — Windows TCI level too hot for JTDX.

## Test protocol — "no TCI audio into WSJT-X on some bands" (Linux, v26.6.5)

**Context:** #3669 (TCI audio doesn't start on connect) had a fix merged — PR **#3759**
(`c7d93c05`), plus #3767 and #3796 — all in v26.6.5. The original reporter confirmed
(2026-06-29) it's **fixed on Linux**, remains only on **Windows 11**. So a no-audio symptom on
**Linux 26.6.5 is probably a NEW/distinct bug** (likely the per-band variant), not vanilla #3669.
Goal: measure *where* audio stops and *which single control* recovers it, so any report is precise.

### Instrumentation
1. AetherSDR → **Help → Support → Logging**: enable the **CAT** and **DAX** categories (checkmarks),
   then restart AetherSDR. (Default is warnings-only; these unlock the debug lines below.)
2. Log file: `~/.config/AetherSDR/logs/aethersdr.log` (symlink to the live `aethersdr-*.log`).
   Tail it: `tail -f ~/.config/AetherSDR/logs/aethersdr.log`
3. WSJT-X connected via TCI; watch its input level bar.

### The key log line (`TciServer.cpp:1364`, category `aether.cat`)
```
TCI: DAX RX audio dax_ch=N slice=.. receiver=.. in_bytes=.. enabled_clients=.. sent_clients=.. out_frames=.. frames_sent=..
```
Read it on the FAILING band:
- **Line absent / `in_bytes=0`** → radio's DAX audio isn't reaching the TCI server → the DAX
  stream isn't started upstream (the #3669 class — but that's supposedly Linux-fixed, so this
  would be notable).
- **`in_bytes>0` but `enabled_clients=0`** → WSJT-X isn't subscribed for TCI audio (client/handshake).
- **`in_bytes>0`, `enabled_clients>0`, `sent_clients=0`** → server has audio, isn't forwarding.
- **`frames_sent` climbing** but WSJT-X bar flat → audio IS reaching WSJT-X → it's a **level**
  problem (too low/hot), not a missing stream → points at per-band AGC/level, not routing.

### Isolate the trigger (change ONE thing at a time)
1. **Baseline (good band):** record PC Audio (on/off), AF value, SQL (on/off/level), AGC mode,
   mode (USB/DIGU). Confirm log `frames_sent` climbing + WSJT-X decoding.
2. **Switch to a bad band. Before touching anything**, record the same 5 states + which log
   bucket above applies + WSJT-X bar.
3. Then, one at a time (re-check audio + log after each): (a) mode USB→DIGU→USB, (b) DAX off/on,
   (c) PC Audio off/on, (d) AGC mode change, (e) AF move. **Note which single action restores audio.**
4. Re-enter the bad band 3× — is it deterministic per band, or intermittent?

### Filing decision from the result
- **DAX audio never starts on the band (in_bytes=0), recovered by mode/DAX toggle, on Linux 26.6.5**
  → new report (a #3669 regression/variant that survives the Linux fix). Attach the CAT+DAX log.
- **Band profile brings up PC Audio off or SQL on** → new bug: profile-load resets audio state
  per band (adjacent to #3263). File fresh.
- **frames_sent was climbing; only AGC/level change made WSJT-X decode** → per-band level/AGC
  report, different class.
- Grab a **support bundle** (Help → Support) for whichever you file.

## Key code references
- TCI audio from DAX: `src/gui/MainWindow_Session.cpp:1470-1479`
- TCI RX audio ingest: `src/core/TciServer.cpp:1065` (`onRxAudioReady`), `onDaxAudioReady`
- AF/AGC/SQL controls: `src/gui/RxApplet.cpp` (AF ~1155, AGC ~987)
- Kiwi audio → speaker mix: `src/core/AudioEngine.cpp:~1185`; Kiwi waterfall calibration note:
  `docs/kiwisdr-cleanroom-design.md`
