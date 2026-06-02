#!/usr/bin/env python3
"""
Watch mode for compile_commands.json

Watches source and include dirs defined in project.yaml and regenerates
compile_commands.json on any change. Ctrl+C to stop.

Usage:
    python compile_commands_watch.py
    python compile_commands_watch.py --config project.yaml --output compile_commands.json
"""

import argparse
import json
import time
from pathlib import Path

from compile_commands_generate import generate_compile_commands, load_config


def _snapshot(dirs: list[Path], extensions: list[str]) -> dict[str, float]:
    """Capture mtimes of all watched files."""
    mtimes = {}
    for d in dirs:
        for ext in extensions:
            for f in d.rglob(f"*{ext}"):
                mtimes[str(f)] = f.stat().st_mtime
    return mtimes


def _watched_dirs(config) -> list[Path]:
    """Extract all source and include dirs from project config."""
    return list(
        {
            Path(d)
            for group in config.get("source_groups", [])
            for key in ("source_dirs", "include_dirs")
            for d in group.get(key, [])
            if Path(d).exists()
        }
    )


def watch(config_file: str, output: str, interval: int = 2) -> None:
    """Watch project dirs and regenerate on any change. Ctrl+C to stop."""
    config = load_config(config_file)

    language = config.get("project", {}).get("language", "c")
    extensions = (
        [".cpp", ".cc", ".cxx"] if language in ["c++", "cpp", "cxx"] else [".c", ".h"]
    )

    dirs = _watched_dirs(config)

    # Generate once immediately on startup
    cmds = generate_compile_commands(config_file)
    with open(output, "w", encoding="utf-8") as f:
        json.dump(cmds, f, indent=2)
    print(f"✓ {output} generated ({len(cmds)} entries)")
    print(f"Watching {len(dirs)} dir(s)... Ctrl+C to stop")

    last = _snapshot(dirs, extensions)

    try:
        while True:
            time.sleep(interval)
            current = _snapshot(dirs, extensions)
            if current != last:
                last = current
                print("Change detected, regenerating...")
                cmds = generate_compile_commands(config_file)
                with open(output, "w", encoding="utf-8") as f:
                    json.dump(cmds, f, indent=2)
                print(f"✓ {output} updated ({len(cmds)} entries)")
    except KeyboardInterrupt:
        print("\nWatch stopped.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Watch project dirs and regenerate compile_commands.json on change"
    )
    parser.add_argument("--config", "-c", default="project.yaml")
    parser.add_argument("--output", "-o", default="compile_commands.json")
    parser.add_argument(
        "--interval",
        "-i",
        type=int,
        default=2,
        help="Poll interval in seconds (default: 2)",
    )

    args = parser.parse_args()
    watch(args.config, args.output, args.interval)


if __name__ == "__main__":
    main()
