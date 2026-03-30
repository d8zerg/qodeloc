#!/usr/bin/env python3
"""Download QodeLoc models through the local Hugging Face Hub CLI.

The script intentionally shells out to `hf download` so it can use the
`pipx`-installed CLI already present on the machine without adding a Python
dependency to the bootstrap phase.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "models" / "catalog.json"


def load_catalog(manifest_path: Path) -> dict:
    with manifest_path.open("r", encoding="utf-8") as handle:
        catalog = json.load(handle)

    if "models" not in catalog:
        raise SystemExit(f"Manifest {manifest_path} is missing the 'models' section.")

    catalog.setdefault("defaults", {})
    return catalog


def resolve_hf_cli() -> str:
    candidates = [
        shutil.which("hf"),
        shutil.which("huggingface-cli"),
        str(Path.home() / ".local" / "bin" / "hf"),
        str(Path.home() / ".local" / "bin" / "huggingface-cli"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return candidate

    raise SystemExit(
        "Could not find the Hugging Face Hub CLI. Install it with pipx or make sure ~/.local/bin is available."
    )


def merge_spec(defaults: dict, spec: dict) -> dict:
    merged = dict(defaults)
    merged.update(spec)
    return merged


def expand_selections(requested: list[str], catalog: dict) -> list[str]:
    models = catalog["models"]

    if not requested:
        raise SystemExit("Provide at least one model short name or 'all'.")

    if "all" in requested:
        return list(models.keys())

    selected: list[str] = []
    seen: set[str] = set()
    for token in requested:
        if token in models:
            items = [token]
        else:
            known_models = ", ".join(models.keys())
            raise SystemExit(f"Unknown model '{token}'. Known models: {known_models}.")

        for item in items:
            if item not in models:
                raise SystemExit(f"Selection '{token}' references unknown model '{item}'.")
            if item not in seen:
                selected.append(item)
                seen.add(item)

    return selected


def format_includes(include: list[str]) -> str:
    if not include:
        return "full repo"
    return ", ".join(include)


def print_catalog(catalog: dict) -> None:
    defaults = catalog.get("defaults", {})
    models = catalog["models"]

    print("Available model installs:")
    for name, raw_spec in models.items():
        spec = merge_spec(defaults, raw_spec)
        include = spec.get("include", [])
        vram = spec.get("approx_vram_gb", "?")
        kind = spec.get("kind", "unknown")
        repo_id = spec["repo_id"]
        notes = spec.get("notes", "")
        print(f"- {name:14} | {repo_id} | {kind} | ~{vram} GB | {format_includes(include)}")
        if notes:
            print(f"  {notes}")


def run_download(hf_cli: str, model_name: str, spec: dict, dry_run: bool, index: int, total: int) -> None:
    repo_id = spec["repo_id"]
    revision = spec.get("revision", "main")
    include = spec.get("include", [])
    local_dir = (ROOT / spec["local_dir"]).resolve()
    cache_dir = (ROOT / spec.get("cache_dir", "models/cache/hf")).resolve()

    local_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        hf_cli,
        "download",
        repo_id,
        "--revision",
        revision,
        "--cache-dir",
        str(cache_dir),
        "--local-dir",
        str(local_dir),
    ]
    if dry_run:
        cmd.append("--dry-run")
    for pattern in include:
        cmd.extend(["--include", pattern])

    print(f"[{index}/{total}] {model_name}: {repo_id}", flush=True)
    print(f"    local dir: {local_dir}", flush=True)
    print(f"    cache dir:  {cache_dir}", flush=True)
    print(f"    files:      {format_includes(include)}", flush=True)

    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode) from exc

    print(f"    done", flush=True)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Download QodeLoc models through the Hugging Face Hub CLI."
    )
    parser.add_argument(
        "models",
        nargs="*",
        help="Model short names or 'all'. Use --list to inspect the catalog.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help=f"Path to the model catalog (default: {DEFAULT_MANIFEST}).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Pass --dry-run to hf download and only print what would be fetched.",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="Print the available model catalog and exit.",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    catalog = load_catalog(args.manifest)

    if args.list:
        print_catalog(catalog)
        return 0

    hf_cli = resolve_hf_cli()
    selections = expand_selections(args.models, catalog)
    defaults = catalog.get("defaults", {})

    for index, model_name in enumerate(selections, start=1):
        raw_spec = catalog["models"][model_name]
        spec = merge_spec(defaults, raw_spec)
        if "local_dir" not in spec:
            raise SystemExit(f"Model '{model_name}' is missing the 'local_dir' field.")
        if "repo_id" not in spec:
            raise SystemExit(f"Model '{model_name}' is missing the 'repo_id' field.")
        run_download(hf_cli, model_name, spec, args.dry_run, index, len(selections))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
