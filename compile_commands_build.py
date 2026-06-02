#!/usr/bin/env python3
"""
Simple build script - companion to compile-commands generator.
Compiles your project based on the same project.yaml configuration.
Uses standard C/C++ environment variables for cross-compilation support.
"""

import argparse
import os
import subprocess
import sys
import yaml
from pathlib import Path


def setup_environment(config, mode_config) -> dict[str, str]:
    """Set up environment variables from config (respects existing env vars)."""
    env = os.environ.copy()

    # Determine language
    language = config["project"].get("language", "c")
    is_cpp = language in ["c++", "cpp", "cxx"]

    # CC/CXX: Compiler path (only if not already set)
    compiler_var = "CXX" if is_cpp else "CC"
    if compiler_var not in env:
        compiler_path = config["compiler"].get(
            "compiler_path", "g++" if is_cpp else "gcc"
        )
        env[compiler_var] = compiler_path

    # CFLAGS/CXXFLAGS: Compilation flags (only if not already set)
    flags_var = "CXXFLAGS" if is_cpp else "CFLAGS"
    if flags_var not in env:
        flags: list[str] = config["compiler"].get("flags", [])
        # Add mode-specific flags
        if "extra_flags" in mode_config:
            flags = flags + mode_config["extra_flags"]
        env[flags_var] = " ".join(flags)

    # CPPFLAGS: Preprocessor flags (defines) - only if not already set
    if "CPPFLAGS" not in env:
        defines: list[str] = config["compiler"].get("defines", [])
        cppflags = " ".join(f"-D{d}" for d in defines)
        if cppflags:
            env["CPPFLAGS"] = cppflags

    # LDFLAGS: Linker flags (only if not already set)
    if "LDFLAGS" not in env:
        ldflags: list[str] = []
        if "build" in config and "linker" in config["build"]:
            ldflags.extend(config["build"]["linker"].get("flags", []))
        # Add mode-specific linker flags
        if "linker_flags" in mode_config:
            ldflags.extend(mode_config["linker_flags"])
        if ldflags:
            env["LDFLAGS"] = " ".join(ldflags)

    return env


def get_include_flags(group, config) -> list[str]:
    """Build include directory flags."""
    includes = []

    # Group-specific includes
    for inc_dir in group.get("include_dirs", []):
        includes.append(f"-I{inc_dir}")

    # External includes
    if "dependencies" in config and "external_includes" in config["dependencies"]:
        for ext_inc in config["dependencies"]["external_includes"]:
            includes.append(f"-I{ext_inc}")

    return includes


def find_source_files(source_dirs: list[str]) -> list[str]:
    """Find all C/C++ source files in given directories."""
    extensions = {".c", ".cpp", ".cc", ".cxx"}
    files = []
    for src_dir in source_dirs:
        for src_file in Path(src_dir).rglob("*"):
            if src_file.suffix in extensions:
                files.append(str(src_file))
    return files


def get_source_group_by_name(name: str, source_groups):
    """Find a source group by its name."""
    for group in source_groups:
        if group.get("name") == name:
            return group
    return None


def build_compile_command(
    config, group, source_file: str, output_dir: str, env: dict[str, str]
):
    """Build the compilation command for a single file using env vars."""
    language = config["project"].get("language", "c")
    is_cpp = language in ["c++", "cpp", "cxx"]

    # Use compiler from environment
    compiler = str(env.get("CXX" if is_cpp else "CC"))

    cmd: list[str] = [compiler]

    # Standard
    cmd.append(f"-std={config['project']['standard']}")

    # Flags from environment (already includes mode-specific)
    if is_cpp and "CXXFLAGS" in env:
        cmd.extend(env["CXXFLAGS"].split())
    elif "CFLAGS" in env:
        cmd.extend(env["CFLAGS"].split())

    # Preprocessor flags from environment
    if "CPPFLAGS" in env:
        cmd.extend(env["CPPFLAGS"].split())

    # Group-specific flags
    if "flags" in group:
        cmd.extend(group["flags"])

    # Include directories
    cmd.extend(get_include_flags(group, config))

    # Output object file
    obj_file = Path(output_dir) / (Path(source_file).stem + ".o")
    cmd.extend(["-c", source_file, "-o", str(obj_file)])

    return cmd, obj_file


def build_link_command(
    config, object_files, output_binary: str, env: dict[str, str]
) -> list[str]:
    """Build the linking command using env vars."""
    language = config["project"].get("language", "c")
    is_cpp = language in ["c++", "cpp", "cxx"]

    # Use compiler from environment for linking
    compiler = str(env.get("CXX" if is_cpp else "CC"))

    cmd: list[str] = [compiler]
    cmd.extend([str(obj) for obj in object_files])

    # Linker flags from environment (already includes mode-specific)
    if "LDFLAGS" in env:
        cmd.extend(env["LDFLAGS"].split())

    cmd.extend(["-o", output_binary])

    return cmd


def clean_build_directory(output_dir: str, verbose: bool) -> None:
    """Clean the build directory."""
    output_path = Path(output_dir)

    if not output_path.exists():
        print(f"Build directory '{output_dir}' does not exist, nothing to clean")
        return

    if verbose:
        print(f"Cleaning '{output_dir}'...")

    # Remove all .o files and executables
    removed_count = 0
    for item in output_path.iterdir():
        if item.suffix == ".o" or item.suffix in ["", ".exe", ".elf"]:
            if verbose:
                print(f"  Removing {item}")
            item.unlink()
            removed_count += 1

    # If directory is empty, remove it
    if not list(output_path.iterdir()):
        output_path.rmdir()
        if verbose:
            print(f"  Removed empty directory '{output_dir}'")

    print(f"✓ Cleaned {removed_count} file(s) from '{output_dir}'")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Simple C/C++ project builder using environment variables"
    )
    parser.add_argument(
        "--mode",
        choices=["debug", "release"],
        help="Build mode (debug or release)",
    )
    parser.add_argument("--output", help="Output directory (overrides config)")
    parser.add_argument(
        "--config", default="project.yaml", help="Config file (default: project.yaml)"
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Show environment variables and commands"
    )
    parser.add_argument("--clean", action="store_true", help="Clean build artifacts")

    args = parser.parse_args()

    # Load config
    try:
        with open(args.config, "r") as f:
            config = yaml.safe_load(f)
    except FileNotFoundError:
        print(f"Error: Config file '{args.config}' not found")
        sys.exit(1)

    # Validate build config
    if "build" not in config:
        print("Error: 'build' section missing in config")
        sys.exit(1)

    # Handle clean operation
    if args.clean:
        if not args.mode:
            # Clean all modes
            if "modes" in config["build"]:
                for mode_name in config["build"]["modes"]:
                    mode_cfg = config["build"]["modes"][mode_name]
                    output_dir = mode_cfg.get("output_dir", f"build/{mode_name}")
                    clean_build_directory(output_dir, args.verbose)
            else:
                print("No build modes defined in config")
        else:
            # Clean specific mode
            if (
                "modes" not in config["build"]
                or args.mode not in config["build"]["modes"]
            ):
                print(f"Error: Mode '{args.mode}' not defined in config")
                sys.exit(1)
            mode_cfg = config["build"]["modes"][args.mode]
            output_dir = args.output or mode_cfg.get("output_dir", f"build/{args.mode}")
            clean_build_directory(output_dir, args.verbose)
        return

    # Regular build operation - mode is required
    if not args.mode:
        print(
            "Error: --mode is required for building (use --clean to clean build artifacts)"
        )
        sys.exit(1)

    if "modes" not in config["build"] or args.mode not in config["build"]["modes"]:
        print(f"Error: Mode '{args.mode}' not defined in config")
        sys.exit(1)

    mode_config = config["build"]["modes"][args.mode]

    # Validate that source_groups is specified for this mode
    if "source_groups" not in mode_config:
        print(f"Error: 'source_groups' not specified for mode '{args.mode}'")
        print(
            "  You must explicitly specify which source groups to compile for each mode"
        )
        sys.exit(1)

    mode_source_groups = mode_config["source_groups"]
    if not mode_source_groups:
        print(f"Error: 'source_groups' for mode '{args.mode}' is empty")
        print("  You must specify at least one source group to compile")
        sys.exit(1)

    # Set up environment variables from config
    env = setup_environment(config, mode_config)

    # Determine output directory
    output_dir = args.output or mode_config.get("output_dir", f"build/{args.mode}")

    # Determine output binary name - mode-specific name takes precedence
    output_binary = mode_config.get(
        "output_name", config["build"].get("output", "a.out")
    )

    # Create output directory
    os.makedirs(output_dir, exist_ok=True)

    print(f"Building in {args.mode} mode...")
    print(f"Output directory: {output_dir}")
    print(f"Source groups: {', '.join(mode_source_groups)}")

    if args.verbose:
        print("\nEnvironment variables:")
        for var in ["CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS"]:
            if var in env:
                print(f"  {var}={env[var]}")
        print()

    # Validate all requested source groups exist
    all_group_names = {g.get("name") for g in config.get("source_groups", [])}
    for group_name in mode_source_groups:
        if group_name not in all_group_names:
            print(
                f"Error: Source group '{group_name}' specified in mode '{args.mode}' does not exist"
            )
            print(f"  Available groups: {', '.join(sorted(all_group_names))}")
            sys.exit(1)

    # Gather source files from specified groups only
    all_sources = []
    for group_name in mode_source_groups:
        group = get_source_group_by_name(group_name, config["source_groups"])
        if group:
            group_sources = find_source_files(group["source_dirs"])
            all_sources.extend([(src, group) for src in group_sources])
            if args.verbose:
                print(f"Source group '{group_name}': {len(group_sources)} file(s)")

    if not all_sources:
        print("Error: No source files found in specified source groups")
        sys.exit(1)

    print(f"Found {len(all_sources)} source file(s) total")

    # Compile each source file
    object_files = []
    for source_file, group in all_sources:
        cmd, obj_file = build_compile_command(
            config, group, source_file, output_dir, env
        )

        if args.verbose:
            print(f"Command: {' '.join(cmd)}")

        print(f"Compiling {source_file}...")
        try:
            subprocess.run(cmd, check=True, env=env)
            object_files.append(obj_file)
        except subprocess.CalledProcessError:
            print(f"Error compiling {source_file}")
            sys.exit(1)

    # Link
    output_path = Path(output_dir) / output_binary
    link_cmd = build_link_command(config, object_files, str(output_path), env)

    if args.verbose:
        print(f"Link command: {' '.join(link_cmd)}")

    print(f"Linking {output_binary}...")
    try:
        subprocess.run(link_cmd, check=True, env=env)
    except subprocess.CalledProcessError:
        print("Error linking")
        sys.exit(1)

    print(f"✓ Build complete: {output_path}")


if __name__ == "__main__":
    main()
