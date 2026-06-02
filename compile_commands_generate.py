#!/usr/bin/env python3
"""
Simple compile_commands.json generator for C projects

Reads project.yaml and generates compile_commands.json for LSP servers.
Uses standard C/C++ environment variables for cross-compilation support.
"""

import argparse
import json
import os
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Error: Need 'pip install pyyaml' for YAML support")
    sys.exit(1)


def load_config(config_file: str):
    """Load configuration from YAML file."""
    config_path = Path(config_file)

    if not config_path.exists():
        print(f"Error: Configuration file '{config_file}' not found")
        sys.exit(1)

    try:
        with open(config_file, "r", encoding="utf-8") as f:
            return yaml.safe_load(f)
    except yaml.YAMLError as e:
        print(f"Error: Invalid YAML in '{config_file}': {e}")
        sys.exit(1)


def setup_environment(config) -> dict[str, str]:
    """Set up environment variables from config (respects existing env vars)."""
    env = os.environ.copy()

    # Determine language
    language = config.get("project", {}).get("language", "c")
    is_cpp = language in ["c++", "cpp", "cxx"]

    # CC/CXX: Compiler path (only if not already set)
    compiler_var = "CXX" if is_cpp else "CC"
    if compiler_var not in env:
        compiler_path = config.get("compiler", {}).get(
            "compiler_path", "g++" if is_cpp else "gcc"
        )
        env[compiler_var] = compiler_path

    # CFLAGS/CXXFLAGS: Compilation flags (only if not already set)
    flags_var = "CXXFLAGS" if is_cpp else "CFLAGS"
    if flags_var not in env:
        flags: list[str] = config.get("compiler", {}).get("flags", [])
        env[flags_var] = " ".join(flags)

    # CPPFLAGS: Preprocessor flags (defines) - only if not already set
    if "CPPFLAGS" not in env:
        defines: list[str] = config.get("compiler", {}).get("defines", [])
        cppflags = " ".join(f"-D{d}" for d in defines)
        if cppflags:
            env["CPPFLAGS"] = cppflags

    return env


def find_source_files(
    source_dirs: list[str], extensions: list[str] | None = None
) -> list[Path]:
    """Find all source files in the given directories."""
    if extensions is None:
        extensions = [".c"]

    source_files: list[Path] = []

    for source_dir in source_dirs:
        source_path = Path(source_dir)
        if not source_path.exists():
            print(f"Warning: Source directory '{source_dir}' does not exist")
            continue

        for ext in extensions:
            # Find all files with the given extension
            source_files.extend(source_path.rglob(f"*{ext}"))

    return sorted(source_files)


def build_compile_command(
    file_path: Path, config, group_config, env: dict[str, str]
) -> dict[str, str]:
    """Build a compile command entry for a single file using env vars."""

    # Determine language
    language = config.get("project", {}).get("language", "c")
    is_cpp = language in ["c++", "cpp", "cxx"]

    # Start with the compiler from environment
    compiler = str(env.get("CXX" if is_cpp else "CC"))
    cmd_parts: list[str] = [compiler]

    # Add language standard
    project_config = config.get("project", {})
    if "standard" in project_config:
        cmd_parts.append(f"-std={project_config['standard']}")

    # Add compilation flags from environment
    if is_cpp and "CXXFLAGS" in env:
        cmd_parts.extend(env["CXXFLAGS"].split())
    elif "CFLAGS" in env:
        cmd_parts.extend(env["CFLAGS"].split())

    # Add preprocessor flags from environment
    if "CPPFLAGS" in env:
        cmd_parts.extend(env["CPPFLAGS"].split())

    # Add group-specific flags
    group_flags = group_config.get("flags", [])
    cmd_parts.extend(group_flags)

    # Add include directories
    include_dirs = group_config.get("include_dirs", [])
    for include_dir in include_dirs:
        cmd_parts.append(f"-I{include_dir}")

    # Add external includes
    dependencies_config = config.get("dependencies", {})
    external_includes = dependencies_config.get("external_includes", [])
    for ext_include in external_includes:
        cmd_parts.append(f"-I{ext_include}")

    # Add group-specific defines
    group_defines = group_config.get("defines", [])
    for define in group_defines:
        cmd_parts.append(f"-D{define}")

    # Add compilation flags (compile only, don't link)
    cmd_parts.extend(["-c", "-o"])

    # Generate output path
    build_config = config.get("build", {})
    build_dir = build_config.get("output_dir", "build/")
    output_file = Path(build_dir) / f"{file_path.stem}.o"
    cmd_parts.append(str(output_file))

    # Add the source file
    cmd_parts.append(str(file_path))

    return {
        "directory": str(Path.cwd()),
        "command": " ".join(cmd_parts),
        "file": str(file_path),
    }


def generate_compile_commands(
    config_file: str, verbose: bool = False
) -> list[dict[str, str]]:
    """Generate compile commands from project configuration."""

    # Load configuration from YAML
    config = load_config(config_file)

    # Set up environment variables from config
    env = setup_environment(config)

    if verbose:
        print("Environment variables:")
        for var in ["CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS"]:
            if var in env:
                print(f"  {var}={env[var]}")
        print()

    compile_commands: list[dict[str, str]] = []

    # Determine file extensions based on language
    language = config.get("project", {}).get("language", "c")
    if language in ["c++", "cpp", "cxx"]:
        extensions = [".cpp", ".cc", ".cxx"]
    else:
        extensions = [".c"]

    # Process each source group
    source_groups = config.get("source_groups", [])
    for group in source_groups:
        group_name = group.get("name", "unnamed")
        if verbose:
            print(f"Processing source group: {group_name}")

        # Find source files for this group
        source_dirs = group.get("source_dirs", [])
        source_files = find_source_files(source_dirs, extensions)

        if verbose:
            print(f"  Found {len(source_files)} source files")

        # Generate compile commands for each file
        for source_file in source_files:
            cmd = build_compile_command(source_file, config, group, env)
            compile_commands.append(cmd)
            if verbose:
                print(f"    {source_file}")

    return compile_commands


def main() -> None:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json from project.yaml using environment variables"
    )
    parser.add_argument(
        "--config",
        "-c",
        default="project.yaml",
        help="Configuration file (default: project.yaml)",
    )
    parser.add_argument(
        "--output",
        "-o",
        default="compile_commands.json",
        help="Output file (default: compile_commands.json)",
    )
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")

    args = parser.parse_args()

    if args.verbose:
        print(f"Reading configuration from: {args.config}")

    # Generate compile commands
    compile_commands = generate_compile_commands(args.config, args.verbose)

    if not compile_commands:
        print("Warning: No source files found!")
        return

    # Write to output file
    try:
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump(compile_commands, f, indent=2)
    except OSError as e:
        print(f"Error writing to '{args.output}': {e}")
        sys.exit(1)

    print(f"Generated {args.output} with {len(compile_commands)} entries")

    # Verify the file exists and is valid JSON
    try:
        with open(args.output, "r", encoding="utf-8") as f:
            json.load(f)
        print("✓ Generated file is valid JSON")
    except json.JSONDecodeError as e:
        print(f"✗ Error: Generated file is not valid JSON: {e}")
        sys.exit(1)
    except OSError as e:
        print(f"✗ Error reading generated file: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
