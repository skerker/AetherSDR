# AetherSDR — Contribution Workflow (quick reference)

> Personal checklist for contributing to **`ten9876/AetherSDR`** as an external
> contributor (fork model, pull-only upstream access).
> Distilled from the repo's own `AGENTS.md` (canonical), `CONTRIBUTING.md`,
> `CONSTITUTION.md` (14 principles), and `GOVERNANCE.md` — **those are
> authoritative; if this drifts, they win.** Captured 2026-06-28.

One-time setup (already done): fork `skerker/AetherSDR`; repo-local SSH commit
signing (`~/.ssh/id_ed25519`, registered as a GitHub *signing* key); commit
email = GitHub no-reply `7691216+skerker@users.noreply.github.com`. See
`SESSION-SUMMARY-2026-06-28.md`.

---

## The per-contribution loop

### 1. Pick one issue
Browse open issues (`good first issue` to start). **One issue per PR** — focused
and reviewable.

### 1a. Confirm it's not already claimed
Before starting, check (rule of thumb: **no open/linked PR + no non-AetherClaude
assignee → fair game**):
- **Linked / open PRs** (most important) — GitHub auto-links any PR saying
  `Fixes #NNNN`; see the issue's "Development" sidebar box, or:
  ```bash
  gh issue view NNNN --repo ten9876/AetherSDR          # linked PRs at bottom
  gh pr list --repo ten9876/AetherSDR --state open --search "NNNN"
  ```
- **Assignees** — `gh issue view NNNN --repo ten9876/AetherSDR --json assignees,labels`.
  **AetherClaude (`@aethersdr-agent`) auto-assigns to every issue** (triage signal,
  *not* a claim). A **non-AetherClaude** assignee = someone's active → coordinate.
- **Bot draft PR** — AetherClaude auto-triages and may open a draft; an
  `aethersdr-agent[bot]` **draft is awaiting review, not WIP**. So "no assignee"
  ≠ "no PR" — check linked PRs.
- **Comments** — skim for "I'll take this."

**How you claim it** (external/pull-only, so the AGENTS.md self-assign step isn't
available): **comment "Working on this, PR incoming"** and/or **open a draft PR
early** referencing `Fixes #NNNN`. Those are your visible claim signals.

### 2. RFC gate — decide *before* coding
Open an `[RFC]` issue (label `rfc`) and get it approved **first** if the change is:
- **Visual design** — colors, fonts, spacing, theme, icons
- **Default UX behavior** — what a click / shortcut / gesture does out of the box
- **New default keyboard bindings**
- **New external dependencies** (libraries, frameworks, system packages)
- **Architecture** — new threads, new signal-routing patterns, audio-pipeline changes
- **Platform-specific native integration** touching shared code
- **New feature areas** substantially beyond current scope

**No RFC needed** (just code it): bug fixes with clear root cause; protocol-
compliance fixes; additive shortcuts unassigned by default; docs; build/CI fixes;
**new applets/dialogs that don't change existing UX**; behavior-preserving perf.
When in doubt, open the RFC and ask.

### 3. Sync & branch — always off the latest upstream `main` (never a release tag)

Remote convention in this clone: **`origin` = `ten9876/AetherSDR`** (upstream,
pull-only), **`fork` = `skerker/AetherSDR`** (your push remote). On the Linux
laptop, run `git remote -v` first and adjust names if they differ (the upstream
may be named `upstream` there). The "proper commit" to branch from is **the
current tip of upstream `main`** (Principle X: verify base against current
`origin/main`).

```bash
# 1. Refresh upstream history
git fetch origin

# 2. (tidy) fast-forward your fork's main mirror to upstream, so the fork's
#    default branch isn't stale.  Never commit directly to main.
git checkout main
git merge --ff-only origin/main
git push fork main
#    GitHub one-liner alternative for steps 1–2:
#    gh repo sync skerker/AetherSDR --source ten9876/AetherSDR

# 3. Branch FROM the upstream tip (not from a release tag, not from stale local main)
git checkout -b fix/<slug>-<issue#> origin/main

# 4. Publish the branch to your fork
git push -u fork fix/<slug>-<issue#>
```

Releases (CalVer `vYY.M.x`) are snapshots of `main`; development always targets `main`.

**If `main` moves while you work:** you do **not** need to keep the branch
up to date — squash-merge runs a fresh three-way merge against `main` at merge
time, and stale base is explicitly allowed. If you *do* want to sync (e.g. to
test against current main, or to resolve an overlap flagged by a base check),
use **`git merge`, not `git rebase`** (rebase silently drops adjacent additions
on hot files):
```bash
git fetch origin
git checkout fix/<slug>-<issue#>
git merge origin/main          # resolve conflicts if any; commit the merge (signed)
git push fork fix/<slug>-<issue#>
```
Quick pre-flight base check before pushing/PR (overlap with your edited files):
```bash
git log --oneline fix/<slug>-<issue#>..origin/main -- <files you changed>
```

### 4. Read the area's patterns before touching it
`AGENTS.md` is canonical. Then the relevant pattern doc:
- any `src/gui/` widget → `docs/a11y.md` (`setAccessibleName`, value-change events)
- dialogs → `docs/style/dialog-patterns.md`
- protocol → FlexLib C# (`~/build/FlexLib/`) — authority; write clean-room C++
Mirror the nearest existing code.

### 5. Code to conventions
C++20 / Qt6 idioms · RAII (no naked `new`/`delete`) · **`AppSettings`, never
`QSettings`** · atomic params for cross-thread DSP (never mutex in the audio
callback) · naming (`PascalCase`/`camelCase`/`m_member`/past-tense signals) ·
scope discipline (don't exceed the issue; don't reformat files you aren't changing).

### 6. Commit — signed
- Imperative subject **< 72 chars**, ending with `Principle <N>.`
- Body explains the change; include `Fixes #NNNN`
- Signing + no-reply email already configured repo-local.

### 7. Verify — evidence, not assertion (Principles VIII / XI)
- **CI gate = 3 compile checks:** `build` (Linux/GCC container), `check-windows`,
  `check-macos`. CI does **not** run unit tests; adding tests is optional.
- **Functional test on a real FlexRadio**; never break the core RX path
  (discovery → connect → FFT → audio).
- **GUI proof:** Automation Bridge — `AETHER_AUTOMATION=1 ./build/AetherSDR` +
  `tools/automation_probe.py` (`dumpTree` / `invoke` / `get`). Deterministic, no
  screenshots. Full ref: `docs/automation-bridge.md`.
- If you add a `find_package(...)`, also add the `-dev` package to
  `.github/docker/Dockerfile` (and let `docker-ci-image.yml` rebuild the image).

### 8. Open the PR (from the fork)
```bash
gh pr create --repo ten9876/AetherSDR \
  --base main --head skerker:fix/<slug>-<issue#> \
  --title "<imperative summary> (Fixes #NNNN)" --body-file <body>
```
- Reference `Fixes #NNNN`; describe the change + your test evidence.
- **Self-approval is blocked** — a maintainer reviews even your own work.
- Stale base is fine (no rebase requirement). If you do sync, use **`git merge`,
  not rebase** (rebase silently drops adjacent additions on hot files).

---

## Who reviews what (CODEOWNERS tiers)
| Tier | Paths | Approvers |
|---|---|---|
| **Default** | everything else | @ten9876, @jensenpat |
| **Maintainer-only** | `MainWindow.{h,cpp}`, `RadioModel.{h,cpp}`, `AudioEngine.{h,cpp}`, `PanadapterStream.{h,cpp}`, `CMakeLists.txt`, `CLAUDE.md`, `CONTRIBUTING.md`, `.github/CODEOWNERS`, `.github/workflows/` | **@ten9876 only** |
| **Mechanical** | `tests/`, `docs/`, `*.md`, Dockerfile, ISSUE_TEMPLATE | + @AetherClaude bot |

Touching a maintainer-only path = expect maintainer scrutiny regardless of author.

## Hard "won't accept" list
Wine/Crossover workarounds · copied proprietary code (clean-room only) · breaking
core RX · large reformatting PRs · UX/visual/architecture changes without an
approved RFC.

## Growing your role (GOVERNANCE.md)
**Contributor** (anyone who opens a PR) → request **Triager** anytime → after
**3+ merged PRs in an area** + demonstrated judgment, request **Domain
Maintainer** for that area (`[Governance] Domain Maintainer request: <area>`) to
merge within it without waiting. Final authority on design/direction stays with
the Project Maintainer (Jeremy Fielder / KK7GWY).

---

**Mental model:** RFC-gate → branch off `main` → read the area's patterns →
mirror existing code → signed commit citing a principle → prove it (CI + radio +
bridge) → fork PR → maintainer review.
