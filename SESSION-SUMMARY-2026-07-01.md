# Session Summary — 2026-07-01 (Mac)

**Machine:** Mac (analysis/triage/docs only — no build/run).
**Topic:** AetherSDR KiwiSDR + TCI/DAX audio-path triage, following KiwiSDR testing on Linux 2026-06-30.
**Repo:** canonical is now **`aethersdr/AetherSDR`** (old `ten9876/AetherSDR` redirects; local
`origin`=ten9876, `fork`=skerker still work).
**Build under discussion:** **v26.6.5 @ `f99842e8`** (2026-06-30). Tested on Linux; Mac checkout same commit.

---

## Outcomes this session
- **Filed #3950** — Kiwi connection never released on slice switch-away/clear (squats user slot +
  burns per-IP time budget). Bot triaged & **confirmed** the fix, flagged an **ordering caveat**
  (see below). Currently `maintainer-review`, **awaiting `aetherclaude-eligible`**.
- **Commented on RFC #3894** — Kiwi RX→WSJT-X audio export use case (hybrid: Flex TX + Kiwi decode
  + self-spotting); asked `supportsRxAudioExport` cover the Flex-backed path (#3613), not just standalone.
- **Wrote/updated 3 reference docs** (meta repo): `AETHERSDR-AUDIO-PATH.md` (new), additions to
  `AETHERSDR-CONTRIBUTING-WORKFLOW.md`; this summary + `linux-next-steps` handoff branch on the fork.

## Key findings (verified against code @ f99842e8)

1. **Kiwi connect reliability up** — matches #3790 fix (TLS-only new-pool proxies).
2. **Kiwis don't disconnect on switch** — REAL bug → **#3950**. `assignSliceToProfile`/
   `clearSliceAssignment` only mute; `disconnectProfile()` + `shouldMaintainProfileConnection()`
   exist but aren't consulted. **Ordering caveat (from bot):** in `assignSliceToProfile`, the gated
   `disconnectProfile(prev)` must go **after** `m_sliceAssignments.insert()` (the predicate reads
   the map). `clearSliceAssignment` is fine at the end. Current-main lines: `433-441`, `475-492`,
   `817`, `345-356`.
3. **Kiwi RX→WSJT-X decode: not a bug, by design** — Kiwi audio is speaker/monitor-only, never
   enters DAX → RFC #3894 (`supportsRxAudioExport`). Commented.
4. **Speaker-only Kiwi is useful as-is** — monitor your own SSB TX on a Kiwi (by ear). → Linux test.
5. **Kiwi waterfall "less sharp / hard to adjust" = NOT a bug** — coarseness is inherent
   (uncalibrated intensity, not dBm); the contrast/floor/Auto controls DO work (re-scoped overlay
   sliders, `SpectrumOverlayMenu.cpp:1908`). Candidate "A" **withdrawn**.

## TCI/DAX/PC/AF audio path (→ `AETHERSDR-AUDIO-PATH.md`)
- **WSJT-X hears the radio's DAX stream, not the speaker path.** TCI audio feeds from DAX
  (`MainWindow_Session.cpp:1471`, #1331), so speaker mute/NR/AF don't touch it.
- **SQL OFF** (radio-side; gates DAX/TCI) and **AGC affects decode** (radio-side). **AF = speaker only.**
- **DAX/TCI feed has NO AGC** — flat per-channel gain (`TciServer.cpp:1279`) + ±1.0 clip; only AGC
  is the slice's. **TCI RX 1-4 and DAX RX 1-4 = same channels, two separate output-path sliders**
  (`TciApplet.cpp:316`) → if WSJT-X uses the DAX *device*, the TCI slider does nothing (likely why
  "TCI slider seems inert"). User's AGC-off + RF-gain approach is correct.
- ⚠️ **OPEN:** whether **PC Audio** must be *enabled* for TCI audio. #1071 comment says yes; field
  behavior says PC-Audio-off only mutes speaker while WSJT-X keeps decoding. **Unresolved — test.**

## #3669 status (asked)
Fix merged (**PR #3759** `c7d93c05`, +#3767 +#3796) — in v26.6.5. Reporter confirmed 2026-06-29
it's **fixed on Linux, Windows-only now.** ⇒ a no-TCI-audio-per-band symptom on **Linux 26.6.5 is
likely a NEW/distinct bug**, not vanilla #3669.

## Contributing workflow (→ `AETHERSDR-CONTRIBUTING-WORKFLOW.md`)
Two paths to land a fix: **A) bot** (maintainer adds `aetherclaude-eligible` → AetherClaude PRs;
median ~10h, ~76% within 3 days, **not guaranteed**) vs **B) human PR** (no label needed).
**Plan for #3950:** wait 2–3 days for the label; if none, do Path B. **Collision-proof Path B:**
comment *"taking to a human PR — please hold off on `aetherclaude-eligible`, PR incoming"* + open an
early **draft PR** with `Fixes #3950` (machine-readable claim); those first, never work silently.

---

## NEXT STEPS ON LINUX (v26.6.5)
1. **Verify #3816 freeze gone** (assign public Kiwi; no runaway/Flex drop).
2. **Reproduce #3950** (switch Kiwi A→B; A still Connected + counts as user on A).
3. **Test: does Kiwi audio survive TX?** (monitor own SSB on Kiwi; if muted on MOX/PTT → new bug).
4. **HIGH: no-TCI-audio-per-band test protocol** (in `AETHERSDR-AUDIO-PATH.md`): enable CAT+DAX
   logging, tail `~/.config/AetherSDR/logs/aethersdr.log`, read `TCI: DAX RX audio … in_bytes/
   enabled_clients/sent_clients/frames_sent`, change one control at a time. If reproduced on Linux
   → likely NEW bug (file fresh), not #3669.
5. **Level/AGC control tests T1–T5** (understand DAX/TCI/RF-gain knobs; T1 = which path WSJT-X uses).
6. **#3950:** after 2–3 days, if not `aetherclaude-eligible`, open Path-B PR (mind ordering caveat;
   load signing key `ssh-add --apple-use-keychain ~/.ssh/id_ed25519`).

## Issue reference map
#3950 (FILED) · #3894 (commented) · #3613 (shipped Kiwi design) · #3669 (Linux-fixed via #3759,
Win-only) · #3816 (freeze, fixed #3825) · #3790 (connect fix) · #3715/#3305 (lifecycle/DAX RFCs) ·
#1331 (TCI-from-DAX) · #1071 (PC-Audio-vs-stream).

---
*Docs this session (meta repo): `AETHERSDR-AUDIO-PATH.md`, `AETHERSDR-CONTRIBUTING-WORKFLOW.md`
(two-paths + collision-proof), this summary. Memory: `project_aethersdr_contributing` updated.*
