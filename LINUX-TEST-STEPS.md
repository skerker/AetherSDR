# Linux test steps — BNR Intensity slider (issue #3815)

Self-contained checklist for verifying the fix on the Ubuntu 24.04 / RTX 5060
laptop, then opening the PR. **This file lives only on the `linux-test-steps`
branch of the fork — it is intentionally kept off the PR branch
(`fix/bnr-intensity-slider-3815`) so the pull request stays code-only.**

- **Fork:** https://github.com/skerker/AetherSDR
- **PR branch:** `fix/bnr-intensity-slider-3815`
- **Fix commit:** `97dc3168` — "Restore BNR Intensity slider in AetherDSP BNR tab. Principle III."
- **Upstream issue:** ten9876/AetherSDR#3815 (`good first issue`, `bug`, `GUI`, `audio`)

---

## What the change does (GUI-only)

The #2297 DSP refactor removed the Spectrum Overlay DSP sub-panel that hosted
BNR's 0–100% Intensity slider, but the control was never re-created elsewhere,
so BNR has run pinned at its hardcoded 100% default. This restores it by
mirroring the DFNR tab.

Files touched (all `src/gui/`): `AetherDspWidget.{h,cpp}`,
`MainWindow_Wiring.cpp`, `AetherDspDialog.{h,cpp}`. No protocol/backend change —
`AudioEngine::setBnrIntensity` → gRPC `EnhanceAudioConfig.intensity_ratio`
already existed.

---

## 1. Get the branch

From the existing AetherSDR clone on the laptop:

```bash
# add the fork remote (one-time; ignore error if it already exists)
git remote add fork https://github.com/skerker/AetherSDR.git 2>/dev/null
git fetch fork

# check out the PR branch and confirm the commit
git checkout fix/bnr-intensity-slider-3815
git log -1 --oneline        # expect: 97dc3168 Restore BNR Intensity slider...
```

## 2. Rebuild with BNR enabled

```bash
# incremental rebuild using the existing -DENABLE_BNR=ON build dir (fast)
cmake --build build -j$(nproc)
```

If `build/` isn't the BNR-enabled dir, reconfigure first (adjust the Qt prefix):

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64" \
  -DAETHER_GPU_SPECTRUM=ON -DENABLE_BNR=ON
cmake --build build -j$(nproc)
```

## 3. Run + verify (with the Maxine BNR container running, connected to the Flex)

Open **AetherDSP → BNR** tab and confirm:

- [ ] **Slider present** — an **Intensity** slider (0–100%) with a value label
      and a Reset-defaults button. The old "controlled from the slice overlay
      menu" text is gone.
- [ ] **Audible effect** — enable BNR on an SSB slice, drag Intensity down:
      denoising audibly weakens; GPU util tracks it. 0% ≈ passthrough, 100% = full.
- [ ] **Persistence** — set e.g. 40%, restart AetherSDR → slider returns at 40%
      (stored as `BnrIntensity` in `~/.config/AetherSDR/AetherSDR.settings`).
- [ ] **Reset** — Reset-defaults snaps back to 100%.

### 3b. (Recommended) Deterministic check via the Automation Bridge

AGENTS.md §"Agent Automation Bridge" — the project's no-screenshot way to prove
a GUI control exists and is drivable. Launch with the bridge on:

```bash
AETHER_AUTOMATION=1 ./build/AetherSDR &
# in another shell:
python3 tools/automation_probe.py dumpTree | grep -i 'BNR Intensity'
#   → expect the slider: accessibleName "BNR Intensity", value, range 0–100
python3 tools/automation_probe.py invoke 'BNR Intensity' setValue 40
python3 tools/automation_probe.py dumpTree | grep -i 'BNR Intensity'   # value=40
```

Full reference: `docs/automation-bridge.md`. This gives a clean "snapshot → act
→ assert" record to paste into the PR alongside the audible result.

---

## Test requirements before the PR (verified against AGENTS.md + CI)

| Requirement | Source | Status / how |
|---|---|---|
| Compiles **Linux** (GCC, container) | `ci.yml` `build` — required | the PR triggers it; GUI-only change |
| Compiles **Windows** (MSVC) | `ci.yml` `check-windows` — required | no platform guards added |
| Compiles **macOS** | `ci.yml` `check-macos` — required | — |
| Functional test on real FlexRadio | CONTRIBUTING §"Submitting Code" #5 | steps 3 / 3b above |
| Core RX path not broken | CONTRIBUTING §"What We Will Not Accept" | sanity-check: discovery → connect → FFT → audio still work |
| a11y patterns | AGENTS.md §Accessibility (`a11y-check.yml`) | slider has `setAccessibleName`; check is warning-only |
| Signed commit | branch protection | done (`97dc3168`, SSH-signed, Verified) |

**Not required:** running the unit tests / `ctest` — CI does **not** run them
(the `build` job only configures + compiles). Adding a widget unit test is
optional, and there's no existing `AetherDspWidget` test to extend.

**No Dockerfile change needed:** AGENTS.md CI/CD rule only applies if you add a
new `find_package(...)` — this change adds none.

**Includes:** per AGENTS.md (Linux CI floor is Qt 6.4.2) sibling TUs must carry
includes explicitly — this change adds no new includes (`QGridLayout`,
`QHBoxLayout`, `QSlider`, `QLabel`, `AppSettings`, `ThemeManager` are all
already used in `AetherDspWidget.cpp`).

---

## 4. Report back

Tell Claude what you observed (especially the audible change in step 3). That
becomes the "confirmed working in practice" evidence for the PR body
(Constitution Principle XI: Fixes Are Demonstrated).

## 5. Open the PR (Claude will do this once you confirm)

- Base: `ten9876/AetherSDR` `main` ← head: `skerker:fix/bnr-intensity-slider-3815`
- Body references `Fixes #3815`, mirrors the issue's evidence, notes it's a
  Default-tier (not maintainer-only) `src/gui` change.
- Command for reference:
  ```bash
  gh pr create --repo ten9876/AetherSDR \
    --base main --head skerker:fix/bnr-intensity-slider-3815 \
    --title "Restore BNR Intensity slider in AetherDSP BNR tab (Fixes #3815)" \
    --body-file <generated>
  ```

---

## Notes / gotchas

- **Commit signing** is configured repo-local on the Mac (SSH, key `id_ed25519`,
  no-reply email `7691216+skerker@users.noreply.github.com`). On the laptop,
  make sure your commits there are also signed + use the no-reply email if you
  commit anything, so the PR's "require signed commits" gate stays satisfied and
  your personal email isn't exposed.
- You have **pull-only** access to upstream, so the AGENTS.md issue/PR
  self-assign step isn't available — fine for a fork PR; `Fixes #3815` links it.
- This branch (`linux-test-steps`) can be deleted from the fork after the PR is up.
