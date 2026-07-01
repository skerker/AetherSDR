# Session Summary — 2026-07-01 (Mac)

**Machine:** Mac (analysis/triage only — no build/run this session).
**Topic:** AetherSDR KiwiSDR triage, following up on Linux testing done 2026-06-30.
**Repo:** canonical is now **`aethersdr/AetherSDR`** (old `ten9876/AetherSDR` remote redirects there; local `origin`=ten9876, `fork`=skerker still work fine).
**Build under discussion:** **v26.6.5 @ `f99842e8`** (2026-06-30, "fix(audio): cut RX speaker latency… #3897"). Tested on the Linux laptop; Mac checkout is the same commit.

---

## What prompted this session
From KiwiSDR testing on Linux (2026-06-30):
1. Kiwis now connect much better — could connect to various SDRs.
2. **But they never seem to disconnect** even when switching between them.
3. **Couldn't get a Kiwi RX slice's audio into WSJT-X for decode** while a TX slice was on the antenna.

Also asked: is WebSDR covered too?

---

## Findings (verified against code at `f99842e8`)

### 1. Connect reliability improved — matches a shipped fix
Better connectivity lines up with **#3790** (closed): new-pool KiwiSDR proxies (`NNNNN.proxy.kiwisdr.com`) are TLS-only; preflight learned `wss://…:443` worked but still opened plain `ws://…:8073`. Fix reuses the preflight's transport decision.

### 2. Kiwis don't disconnect on switch — REAL BUG → filed #3950
**Confirmed in code.** Switching a slice from Kiwi A → Kiwi B (or clearing the Kiwi antenna) only **mutes** the previous Kiwi; it never closes the WebSocket. So it keeps showing "Connected" and keeps holding that receiver's user slot until radio-disconnect / profile-delete / explicit disconnect / app quit.

- `KiwiSdrManager::assignSliceToProfile()` prev-profile branch — `src/core/KiwiSdrManager.cpp:401-409` → only `setAudioActive(false)`, no `disconnectProfile()`.
- `KiwiSdrManager::clearSliceAssignment()` — `:443-463` → same.
- Policy verdict already exists but isn't acted on: `shouldMaintainProfileConnection()` — `:774` (`autoConnect || assignedSliceForProfile(id) >= 0`).
- `disconnectProfile()` — `:314` already does correct teardown (cancel reconnect, `disconnectFromEndpoint()` closes socket/frees slot, mutes; leaves saved profile intact).

**Impact:** blocks other users (slot squatting) + burns per-IP time budget (e.g. 1 hr/day) → self-lockout.
**Proposed two-line fix:** in both functions, after muting: `if (!shouldMaintainProfileConnection(prev)) disconnectProfile(prev);` (design choice: immediate vs ~5–10 s grace timer via existing `m_reconnectTimers`).
**Filed:** **#3950** (as @skerker, no labels). Framed so the "disconnect-when-idle" requirement survives even if RFC #3894 reworks the Kiwi architecture.

### 3. Kiwi RX audio → WSJT-X: NOT a bug — by design (speaker-only)
Decoded Kiwi audio is mixed into the **speaker/monitor path only** (`AudioEngine.cpp:1185`+, fed by `KiwiSdrManager::decodedAudioReady`); it **never enters DAX**, so WSJT-X can't capture it. Consistent with #3613's scope: *"a receive-only convenience, not a second radio."*
→ So it's a **capability request, not a bug**. The enabling capability (`supportsRxAudioExport`) is proposed in **RFC #3894**.
**Action:** commented on **#3894** (comment `4858287904`) with the hybrid use case: Flex connected+transmitting locally + Kiwi RX exported to WSJT-X for decode, incl. **self-spotting**; asked that `supportsRxAudioExport` cover the Flex-backed virtual-RX-antenna path (#3613), not just no-Flex standalone. Overlaps @jensenpat's braindump ("hear your own signal on two geographic sources").

### 4. Speaker-only architecture IS useful as-is
Insight: transmit SSB and **monitor your own audio on the Kiwi** by ear (no decode needed) — likely the original motivation for the speaker-only design; complements TX-audio-shaping features (Aetherial Channel Strip, ESSB/VST #662, AM asymmetry #2255).

### 5. WebSDR
Covered alongside KiwiSDR in the same RFCs (#3613 broadened WebSDR→+KiwiSDR; #3894 lists WebSDR as a future provider). Only KiwiSDR is implemented so far.

---

## NEXT STEPS ON LINUX (v26.6.5, laptop)

1. **Verify #3816 freeze is gone** — assign a public Kiwi; confirm no runaway main-thread stall / Flex drop (fix #3825 moved Kiwi client to its own QThread).
2. **Reproduce/confirm #3950 empirically** — assign Kiwi A → switch slice to Kiwi B → confirm A still shows Connected and still counts as a user on A's server (check the receiver's user count). Also test clearing the antenna leaves it connected.
3. **TEST ITEM — does Kiwi audio survive TX?** Key up SSB and confirm the Kiwi audio does **not** mute/duck during MOX/PTT (needed for the "hear yourself on the Kiwi" use case). Kiwi is network-independent of the Flex, but in-app it's a separate `AudioEngine` source that could get ducked on TX. If it mutes → **legit new bug, distinct from #3950.** (Note: Kiwi path ~360 ms+ latency — audio-quality check, not real-time earback.)
4. **Optional: implement the #3950 fix as a starter PR** — branch off `main` via fork per `AETHERSDR-CONTRIBUTING-WORKFLOW.md`; two-line change + decide immediate vs grace-timer; signed commit; 3-check CI gate.
5. **Continue hunting new-issue PRs** (claim-check + workflow doc).

---

## Issue reference map
- **#3950** — Kiwi idle-disconnect bug — **FILED this session.**
- **#3894** — RFC standalone receive sessions — **commented this session** (RX audio export use case).
- **#3613** — RFC remote RF-quiet RX antennas (WebSDR+KiwiSDR) — the shipped speaker-only design.
- **#3816** — Kiwi assign → main-thread freeze (CLOSED; fix #3825, shipped v26.6.5).
- **#3790** — some Kiwis fail to connect / TLS-only new-pool proxies (CLOSED, fix identified).
- **#3715 / #3305** — slice-lifecycle / DAX-stream-ownership RFCs (architectural context for #3950).

---
*Written on Mac 2026-07-01 for handoff to the Linux laptop. See workspace docs `AETHERSDR-CONTRIBUTING-WORKFLOW.md` and `AETHERSDR-BUILD-TOOLCHAIN.md`. Memory updated: `project_aethersdr_contributing`.*
