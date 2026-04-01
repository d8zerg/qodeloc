#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def run_command(args: list[str], *, cwd: Path | None = None) -> None:
    subprocess.run(args, cwd=str(cwd) if cwd is not None else None, check=True)


def git_output(args: list[str], *, cwd: Path) -> str:
    completed = subprocess.run(
        args,
        cwd=str(cwd),
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def clone_or_update_repo(url: str, ref: str, dest: Path, depth: int) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    git = shutil.which("git")
    if git is None:
        raise SystemExit("git is required to fetch the testdata repository")

    if dest.exists():
        if not (dest / ".git").exists():
            raise SystemExit(f"{dest} exists but is not a git repository")
        print(f"[update] {dest} <- {url}@{ref}", flush=True)
        run_command([git, "-C", str(dest), "fetch", "--depth", str(depth), "origin", ref])
        run_command([git, "-C", str(dest), "checkout", "--detach", "FETCH_HEAD"])
        run_command([git, "-C", str(dest), "reset", "--hard", "FETCH_HEAD"])
    else:
        print(f"[clone] {url}@{ref} -> {dest}", flush=True)
        run_command(
            [
                git,
                "clone",
                "--depth",
                str(depth),
                "--branch",
                ref,
                "--single-branch",
                url,
                str(dest),
            ]
        )

    revision = git_output([git, "rev-parse", "HEAD"], cwd=dest)
    tracked_files = git_output([git, "ls-files"], cwd=dest).splitlines()
    print(f"[ready] {dest}", flush=True)
    print(f"revision: {revision}", flush=True)
    print(f"tracked_files: {len(tracked_files)}", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Fetch a public GitHub repository into testdata.")
    parser.add_argument("--url", required=True, help="Git repository URL")
    parser.add_argument("--ref", required=True, help="Branch or tag to clone")
    parser.add_argument("--dest", required=True, help="Destination directory")
    parser.add_argument("--depth", type=int, default=1, help="Clone depth")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    clone_or_update_repo(args.url, args.ref, Path(args.dest), args.depth)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
