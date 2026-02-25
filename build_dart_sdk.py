#!/usr/bin/env python3
"""
Download/sync dart-sdk, apply local patch, and build static-link targets.

Run from repository root:
  python3 scripts/build_dart_sdk.py
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List


def log(msg: str) -> None:
    print(msg, flush=True)


def run(
    cmd: List[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    verbose: bool = False,
) -> None:
    if verbose:
        where = f" (cwd={cwd})" if cwd else ""
        log(f"[run]{where}: {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def run_capture(
    cmd: List[str], *, cwd: Path | None = None, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            cmd,
            cwd=cwd,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as e:
        return subprocess.CompletedProcess(
            cmd,
            127,
            stdout="",
            stderr=str(e),
        )


def ensure_repo_root(root: Path) -> None:
    if not (root / ".dart_version").exists():
        raise RuntimeError("Missing .dart_version. Run from repository root.")
    if not (root / "dart_sdk.patch").exists():
        raise RuntimeError("Missing dart_sdk.patch. Run from repository root.")


def read_dart_version(root: Path) -> str:
    for line in (root / ".dart_version").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    raise RuntimeError("No Dart version found in .dart_version")


def prepend_path(env: dict[str, str], path: Path) -> None:
    sep = ";" if os.name == "nt" else ":"
    old_path = env.get("PATH", "")
    env["PATH"] = f"{str(path)}{sep}{old_path}" if old_path else str(path)


def _has_tool(name: str, env: dict[str, str]) -> bool:
    # Only probe PATH presence here. Running `<tool> --version` can block in
    # some depot_tools environments (network/bootstrap checks).
    return shutil.which(name, path=env.get("PATH")) is not None


def ensure_depot_tools(
    root: Path,
    depot_tools_dir: Path,
    env: dict[str, str],
    verbose: bool,
    force_depot_tools: bool,
) -> None:
    if not force_depot_tools and _has_tool("gclient", env) and _has_tool("fetch", env):
        return

    if not depot_tools_dir.exists():
        log(f"[info] depot_tools not found, cloning to {depot_tools_dir}")
        run(
            [
                "git",
                "clone",
                "https://chromium.googlesource.com/chromium/tools/depot_tools.git",
                str(depot_tools_dir),
            ],
            cwd=root,
            env=env,
            verbose=verbose,
        )
    prepend_path(env, depot_tools_dir)
    if not _has_tool("gclient", env) or not _has_tool("fetch", env):
        gclient_check = run_capture(["gclient", "--version"], env=env)
        fetch_check = run_capture(["fetch", "--version"], env=env)
        raise RuntimeError(
            "Failed to run depot_tools commands after preparing depot_tools:\n"
            f"[gclient]\n{gclient_check.stdout}\n{gclient_check.stderr}\n"
            f"[fetch]\n{fetch_check.stdout}\n{fetch_check.stderr}"
        )


def fetch_or_sync_dart_sdk(
    root: Path,
    dart_sdk_dir: Path,
    dart_version: str,
    env: dict[str, str],
    verbose: bool,
    skip_fetch: bool,
) -> None:
    if skip_fetch:
        log("[info] skip fetch/sync requested")
        return

    if not dart_sdk_dir.exists():
        log(f"[info] dart-sdk not found, fetching into {dart_sdk_dir}")
        dart_sdk_dir.mkdir(parents=True, exist_ok=True)
        run(["fetch", "--no-history", "dart"], cwd=dart_sdk_dir, env=env, verbose=verbose)

    sdk_repo = dart_sdk_dir / "sdk"
    if not sdk_repo.exists():
        raise RuntimeError(f"Expected sdk repo at {sdk_repo}, but it was not found.")

    log(f"[info] checkout dart tag {dart_version}")
    run(
        ["git", "fetch", "origin", f"refs/tags/{dart_version}:refs/tags/{dart_version}"],
        cwd=sdk_repo,
        env=env,
        verbose=verbose,
    )
    run(["git", "checkout", "-f", f"tags/{dart_version}"], cwd=sdk_repo, env=env, verbose=verbose)
    run(["gclient", "sync", "-D", "--no-history"], cwd=sdk_repo, env=env, verbose=verbose)


def apply_patch(root: Path, sdk_repo: Path, patch_file: Path, env: dict[str, str], verbose: bool, skip_patch: bool) -> None:
    if skip_patch:
        log("[info] skip patch requested")
        return

    rel_patch = os.path.relpath(patch_file, sdk_repo)
    check = run_capture(["git", "apply", "--check", rel_patch], cwd=sdk_repo, env=env)
    if check.returncode == 0:
        log(f"[info] applying patch: {patch_file}")
        run(
            ["git", "apply", "--whitespace=fix", rel_patch],
            cwd=sdk_repo,
            env=env,
            verbose=verbose,
        )
        return

    reverse_check = run_capture(
        ["git", "apply", "--reverse", "--check", rel_patch], cwd=sdk_repo, env=env
    )
    if reverse_check.returncode == 0:
        log("[info] patch already applied, skipping")
        return

    raise RuntimeError(
        "Patch cannot be applied and is not already applied.\n"
        f"[check]\n{check.stdout}\n{check.stderr}\n"
        f"[reverse-check]\n{reverse_check.stdout}\n{reverse_check.stderr}"
    )


def iter_build_types(build_type: str) -> Iterable[str]:
    if build_type == "all":
        return ("debug", "release")
    return (build_type,)


def build_targets(
    sdk_repo: Path,
    build_types: Iterable[str],
    targets: List[str],
    env: dict[str, str],
    verbose: bool,
) -> None:
    for mode in build_types:
        log(f"[info] building mode={mode} targets={','.join(targets)}")
        run(
            [sys.executable, "tools/build.py", "-m", mode, *targets],
            cwd=sdk_repo,
            env=env,
            verbose=verbose,
        )


def verify_embedder_archives(sdk_repo: Path) -> None:
    expected = (
        "libdart_embedder_runtime_jit_static.a",
        "libdart_embedder_runtime_aot_precompiled_static.a",
    )
    out_root = sdk_repo / "out"
    found_paths: dict[str, Path] = {}
    for out_dir in sorted(out_root.glob("Release*")):
        bin_dir = out_dir / "obj" / "runtime" / "bin"
        for name in expected:
            candidate = bin_dir / name
            if candidate.exists():
                found_paths[name] = candidate

    missing = [name for name in expected if name not in found_paths]
    if not missing:
        for name in expected:
            log(f"[info] found: {found_paths[name]}")
        return

    observed = []
    for out_dir in sorted(out_root.glob("Release*")):
        bin_dir = out_dir / "obj" / "runtime" / "bin"
        if bin_dir.exists():
            observed.extend(str(p) for p in bin_dir.glob("libdart_embedder_runtime*.a"))
    observed_text = "\n".join(observed) if observed else "(none)"
    raise RuntimeError(
        "Missing required embedder runtime archives:\n"
        + "\n".join(f"  - {m}" for m in missing)
        + "\nObserved runtime archives:\n"
        + observed_text
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch/sync Dart SDK, apply patch, and build static-link targets."
    )
    parser.add_argument(
        "--build-type",
        choices=["debug", "release", "all"],
        default="release",
        help="Build mode(s) passed to tools/build.py",
    )
    parser.add_argument(
        "--target",
        action="append",
        default=[],
        help=(
            "Build target for tools/build.py (repeatable). "
            "Default: libdart_embedder"
        ),
    )
    parser.add_argument(
        "--dart-sdk-dir",
        default="dart-sdk",
        help="Directory where dart-sdk checkout lives",
    )
    parser.add_argument(
        "--depot-tools-dir",
        default="depot_tools",
        help="Directory where depot_tools lives",
    )
    parser.add_argument(
        "--skip-fetch",
        action="store_true",
        help="Skip fetch/sync step",
    )
    parser.add_argument(
        "--skip-patch",
        action="store_true",
        help="Skip patch step",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Verbose command logging",
    )
    parser.add_argument(
        "--force-depot-tools",
        action="store_true",
        help="Force using/cloning local depot_tools even if gclient/fetch exist in PATH",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path.cwd()
    ensure_repo_root(root)

    dart_sdk_dir = (root / args.dart_sdk_dir).resolve()
    depot_tools_dir = (root / args.depot_tools_dir).resolve()
    patch_file = (root / "dart_sdk.patch").resolve()

    env = os.environ.copy()
    if depot_tools_dir.exists():
        prepend_path(env, depot_tools_dir)
    ensure_depot_tools(
        root, depot_tools_dir, env, args.verbose, args.force_depot_tools
    )

    dart_version = read_dart_version(root)
    fetch_or_sync_dart_sdk(
        root, dart_sdk_dir, dart_version, env, args.verbose, args.skip_fetch
    )

    sdk_repo = dart_sdk_dir / "sdk"
    apply_patch(root, sdk_repo, patch_file, env, args.verbose, args.skip_patch)

    requested_targets = args.target if args.target else ["libdart_embedder"]
    targets = list(dict.fromkeys(requested_targets))
    build_targets(
        sdk_repo,
        iter_build_types(args.build_type),
        targets,
        env,
        args.verbose,
    )
    if (
        "libdart_embedder" in targets
        or "runtime/bin:dart_embedder_runtime_jit_static" in targets
        or "runtime/bin:dart_embedder_runtime_aot_precompiled_static" in targets
    ):
        verify_embedder_archives(sdk_repo)

    log("[done] Dart SDK ready and static-link target build finished.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as e:
        log(f"[error] command failed with exit code {e.returncode}")
        raise SystemExit(e.returncode)
    except Exception as e:  # pragma: no cover
        log(f"[error] {e}")
        raise SystemExit(1)
