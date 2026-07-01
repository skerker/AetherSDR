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

> **Repo note:** canonical is now **`aethersdr/AetherSDR`** (the old `ten9876/AetherSDR`
> remote redirects there; `gh`/local `origin` still resolve). Commands below say `ten9876`
> for historical continuity — either works.

---

## Two paths to landing a fix on a triaged issue

Once an issue is triaged (bot has confirmed root cause; label `maintainer-review`), there are
**two ways** it gets fixed. Choose per issue.

### Path A — Let the AetherClaude bot implement it (you wait)
- **Gate:** a **maintainer** adds the **`aetherclaude-eligible`** label. That label is the
  bot's authorization to implement (`CONSTITUTION.md:278`, `AGENTS.md:71`). It is the
  issue-level *claim* mechanism for agent work.
- **You cannot self-apply it** — external/pull-only contributors don't have label-write on
  upstream. So Path A = **wait for maintainer review** → they add the label.
- **What happens then:** AetherClaude picks up the issue, creates a worktree from current
  `main`, implements, and opens a **draft PR** (`aethersdr-agent[bot]`, "awaiting human
  review", not WIP) → maintainer reviews & squash-merges.
- **Cost to you:** zero work; timeline is the maintainer's. Best when the fix is routine and
  you don't need the contribution credit.
- **Typical wait (measured, n=25 labeled issues):** median **~10h** from filing to
  `aetherclaude-eligible`; **~half within 12h, ~76% within 3 days**, tail to ~5 days. **But it's
  not guaranteed** — only ~70 issues *ever* got the label, so many are handled other ways
  (maintainer implements, human PR, deferred, RFC). Rule of thumb: **if not labeled in 2–3 days,
  it probably won't be routed to the bot → switch to Path B.**
- **Tell it apart:** the bot's triage comment ends "*awaiting `aetherclaude-eligible` to
  implement*" — that means it's **waiting for** the label, not that it has it. Check the
  actual label list: `gh issue view NNNN --repo aethersdr/AetherSDR --json labels`.

### Path B — Open a human PR yourself (you implement)
- **No `aetherclaude-eligible` needed** — that label only gates the *bot*. A human contributor
  may open a PR against a triaged issue anytime; `maintainer-review` does **not** block you.
- **Claim it first (coordination, Principle X)** — see "Making Path B collision-proof" below.
- **Then run the per-contribution loop below** (sync → branch off `main` → code → signed
  commit → CI/radio/bridge proof → fork PR).
- Best when you want the contribution credit / it's a good first PR, or you want it landed
  sooner than the maintainer's bot queue.

### Making Path B collision-proof
The only collision risk is: a maintainer adds `aetherclaude-eligible` → the bot implements in
parallel. You preempt that with two visible claim signals (you can't self-assign — external/
pull-only), done **in this order and up front, not after days of silent work**:

1. **Comment that does double duty** — claim it *and* tell them not to route it to the bot:
   > *"I'll take this one to a human PR — please hold off on `aetherclaude-eligible`. PR incoming."*
   Asking them not to add the label removes the collision trigger entirely (the bot only acts
   once labeled).
2. **Open the PR early as a DRAFT with `Fixes #NNNN`** — this is the *machine-readable* signal:
   GitHub auto-links it into the issue's Development box, and the orchestrator checks for existing
   linked PRs before implementing (per `CONTRIBUTING.md` bot-draft/claim logic). A linked open PR
   is what makes other agents route around you. Then finish and mark Ready for Review.

**Belt & suspenders:** the comment asks them not to label; even if someone does, the open
`Fixes #NNNN` PR makes the orchestrator skip it. The race exists **only** in the gap between
"I started" and "there's a visible PR" — so make the comment + draft PR the *first* actions.

- If you start and then stop, say so on the issue so it's reclaimable (Principle X: no resource
  held past a dead effort).

**Rule of thumb:** routine fix you don't care to author → wait for Path A. Want the credit /
a clean starter PR / faster landing → claim it and take Path B.

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
