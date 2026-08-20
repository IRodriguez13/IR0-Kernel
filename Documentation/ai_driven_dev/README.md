# AI-Assisted Development for IR0

This directory is the **tracked, versioned** copy of rules and skills for AI coding
agents (Cursor, Claude Code, Copilot Workspace, etc.) working on the IR0 kernel.

Local IDE configuration under `.cursor/` is **gitignored** and must be installed from
here when setting up a developer machine.

## Why this exists

- Share agent constraints with contributors who do not use Cursor.
- Keep architecture rigor, commit hygiene, and tier roadmap in the repository.
- Avoid coupling project policy to a single vendor IDE layout.

## Layout

| Path | Purpose |
|------|---------|
| `AGENTS.md` | Short agent entrypoint (style, endpoints, tier map) |
| `rules/*.md` | Full rule set (architecture, tiers, git hygiene, multi-agent) |
| `skills/ctr/SKILL.md` | CTR checklist — compile gates and facade discipline |

## Install into Cursor (local only)

From the repository root:

```bash
make ai-dev-rules-install
# or
python3 scripts/sync_ai_dev_rules.py install
```

This copies rules into `.cursor/rules/*.mdc`, the CTR skill into `.cursor/skills/`,
and `AGENTS.md` to the repository root (also gitignored).

## Export after editing local Cursor rules

If you maintain rules in `.cursor/` first:

```bash
python3 scripts/sync_ai_dev_rules.py export
```

Review the diff under `Documentation/ai_driven_dev/` before committing.

## Rule index

| Rule | Scope | Summary |
|------|-------|---------|
| `kernel-architecture-rigor.md` | always | Facades, SPDX, testing, honest docs |
| `kernel-c-allman-style.md` | C sources | Allman brace style |
| `kernel-docs-language-policy.md` | `**/*.md` | English primary; Spanish in `Documentation/esp/` |
| `ir0-git-commit-hygiene.md` | always | Maintainer author only; Signed-off-by; never Co-authored-by |
| `ir0-roadmap-research-multiagent.md` | always | T0–T3 tiers, web research, CTR gates |
| `ir0-development-plan-mode.md` | always | When to plan before coding |
| `ir0-development-multiagent-format.md` | always | Parallel agents and oleada report format |
| `ir0-userspace-monolith-debt.md` | always | Syscall split targets, facade copies |
| `ir0-smoke-autokill.md` | always | QEMU smokes via `scripts/smoke_qemu_run.sh` / `smoke_autokill.py` |
| `ir0-version-stamp.mdc` | always | Lockstep `version.h` / Makefile with upstream tags |
| `oss-kernel-reference.md` | on demand | Cross-check MM/exec/syscall invariants |
| `ir0-tier-t0-os-functional.md` | tier T0 | OS + ktest/contracts scope |
| `ir0-tier-t1-userspace-posix.md` | tier T1 | init + musl + syscalls |
| `ir0-tier-t2-graphics-fullscreen.md` | tier T2 | fb0, input, mmap clients |
| `ir0-tier-t3-desktop-minimal.md` | tier T3 | WM/desktop (planning only) |

## Mandatory agent workflow (CTR)

After substantial kernel changes, agents must run:

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
```

See `skills/ctr/SKILL.md` for the full checklist.

## Other AI tools

- **Generic agents:** point the system prompt at `AGENTS.md` plus relevant `rules/*.md`.
- **CI / review bots:** enforce `ir0-git-commit-hygiene.md` and `kernel-architecture-rigor.md`.
- **Human docs:** subsystem behavior lives in `Documentation/*.md`; this tree is policy only.
