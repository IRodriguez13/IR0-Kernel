#!/usr/bin/env python3
"""
IR0 repository hygiene guardrails.

Checks:
1) No obviously unnecessary artifacts are tracked.
2) No compiled binaries or build outputs under setup/pid1 or vendored BusyBox.
3) No Co-authored-by trailers and no agent Author/Committer in history.
4) Spanish markdown naming/location is constrained to /esp directories.
"""

from pathlib import Path
import fnmatch
import re
import subprocess
import sys

FORBIDDEN_COAUTHOR_RE = re.compile(
    r"^Co-authored-by:",
    re.IGNORECASE | re.MULTILINE,
)

AGENT_IDENTITY_RE = re.compile(
    r"cursoragent@|^\s*Cursor Agent\s*$",
    re.IGNORECASE,
)


ROOT = Path(__file__).resolve().parent.parent

FORBIDDEN_TRACKED_PATTERNS = [
    "*.log",
    "*.tmp",
    "*.swp",
    "*.swo",
    ".DS_Store",
    "qemu_debug.log",
]


def git_ls_files():
    proc = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git ls-files failed")
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


# Paths where compiled artifacts must never be tracked (sources only).
COMPILED_ARTIFACT_PREFIXES = (
    "setup/pid1/fase52_staging/bin/",
    "setup/pid1/fase52_staging/lib/",
)

COMPILED_ARTIFACT_EXACT = {
    "setup/libkconfig_build.a",
    "scripts/kconfig/libkconfig_build.a",
    "setup/pid1/f41true",
    "setup/pid1/fase48_busybox",
    "setup/pid1/fase48_cat",
    "setup/pid1/fase48_echo",
    "setup/pid1/fase50_busybox_real",
    "setup/pid1/fase50_hello",
    "setup/pid1/fase55e_doom_interactive",
    "setup/pid1/fase58c_boot_halt",
    "setup/pid1/fase58c_fbdev",
    "setup/pid1/fase58l_busybox_smoke",
    "setup/pid1/init",
    "setup/pid1/musl_arch_prctl_smoke",
    "setup/pid1/sh_smoke",
    "setup/pid1/userspace_segv",
    "setup/pid1/sbin/irinit",
}

COMPILED_ARTIFACT_SUFFIXES = (
    ".cmd",
    ".a",
)

COMPILED_ARTIFACT_BASENAMES = {
    "busybox",
    "busybox_unstripped",
    "busybox_unstripped.out",
    "applet_tables",
    "usage",
    "usage_pod",
    "docproc",
    "fixdep",
    "split-include",
    "conf",
    "autoconf.h",
    "applet_tables.h",
    "bbconfigopts.h",
    "bbconfigopts_bz2.h",
    "NUM_APPLETS.h",
    "usage_compressed.h",
}


def is_compiled_artifact(path: str) -> bool:
    if path in COMPILED_ARTIFACT_EXACT:
        return True

    name = Path(path).name
    if name in COMPILED_ARTIFACT_BASENAMES:
        if path.startswith(COMPILED_ARTIFACT_PREFIXES[0]):
            return True

    for prefix in COMPILED_ARTIFACT_PREFIXES:
        if not path.startswith(prefix):
            continue
        if prefix in (
            "setup/pid1/fase52_staging/bin/",
            "setup/pid1/fase52_staging/lib/",
        ):
            return True
        if path.endswith(COMPILED_ARTIFACT_SUFFIXES):
            return True
        if name in COMPILED_ARTIFACT_BASENAMES:
            return True
        if "/include/config/" in path and path.startswith(prefix):
            return True

    if path.startswith("setup/pid1/fase52_staging/usr/lib/") and path.endswith(".a"):
        return True

    return False


def is_elf_executable(path: Path) -> bool:
    try:
        data = path.read_bytes()[:4]
    except OSError:
        return False
    return data == b"\x7fELF"


def is_forbidden_tracked(path: str) -> bool:
    name = Path(path).name
    for pattern in FORBIDDEN_TRACKED_PATTERNS:
        if fnmatch.fnmatch(name, pattern) or fnmatch.fnmatch(path, pattern):
            return True
    return False


def is_spanish_named_markdown(path: str) -> bool:
    p = Path(path)
    if p.suffix.lower() != ".md":
        return False
    stem = p.stem.lower()
    return stem.endswith("_es") or stem.endswith("-es") or stem.startswith("esp_")


def git_commits_to_scan():
    """
    Scan commits this branch introduces relative to origin/master.

    Already-merged history cannot be rewritten through a PR when master is
    protected. New agent commits still fail. Use --all-history to audit the
    whole reachable graph (e.g. after an admin rewrite of master).
    """
    extra = sys.argv[1:] if len(sys.argv) > 1 else []
    if "--all-history" in extra:
        spec = ["HEAD"]
    else:
        upstream = subprocess.run(
            ["git", "rev-parse", "--verify", "origin/master"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        if upstream.returncode == 0:
            spec = ["origin/master..HEAD"]
        else:
            spec = ["HEAD"]
    proc = subprocess.run(
        ["git", "log", *spec, "--format=%H"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git log failed")
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def commit_oneline(commit: str) -> str:
    oneline = subprocess.run(
        ["git", "log", "-1", "--oneline", commit],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    return oneline.stdout.strip() if oneline.returncode == 0 else commit


def commits_with_forbidden_attribution():
    """Any Co-authored-by, or Author/Committer using a cloud-agent identity."""
    bad = []
    for commit in git_commits_to_scan():
        ident = subprocess.run(
            ["git", "log", "-1", "--format=%an <%ae>%n%cn <%ce>%n%B", commit],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        if ident.returncode != 0:
            continue
        text = ident.stdout
        label = commit_oneline(commit)
        if FORBIDDEN_COAUTHOR_RE.search(text):
            bad.append(f"{label} (Co-authored-by trailer)")
            continue
        # First two lines are author / committer from --format.
        lines = text.splitlines()
        author = lines[0] if lines else ""
        committer = lines[1] if len(lines) > 1 else ""
        if AGENT_IDENTITY_RE.search(author) or AGENT_IDENTITY_RE.search(committer):
            bad.append(f"{label} (agent Author/Committer)")
    return bad


def main():
    errors = []

    try:
        tracked = git_ls_files()
    except Exception as exc:
        print(f"[repo-hygiene-guard] FAILED: {exc}")
        return 1

    for rel in tracked:
        if is_forbidden_tracked(rel):
            errors.append(f"[tracked-artifact] {rel}")

        if is_compiled_artifact(rel):
            errors.append(f"[tracked-compiled] {rel}")

        full = ROOT / rel
        if full.is_file() and is_elf_executable(full):
            if rel.startswith("setup/pid1/"):
                errors.append(f"[tracked-elf] {rel}")

        if is_spanish_named_markdown(rel):
            parts = Path(rel).parts
            if "esp" not in parts:
                errors.append(f"[spanish-doc-location] {rel} (must live under an /esp directory)")

    try:
        for label in commits_with_forbidden_attribution():
            errors.append(f"[commit-identity] {label}")
    except Exception as exc:
        errors.append(f"[commit-identity-check] {exc}")

    if errors:
        print("[repo-hygiene-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[repo-hygiene-guard] OK")
    return 0


if __name__ == "__main__":
    if "--help" in sys.argv or "-h" in sys.argv:
        print("Usage: repo_hygiene_guard.py [--all-history]")
        raise SystemExit(0)
    raise SystemExit(main())
