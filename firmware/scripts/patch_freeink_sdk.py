# Reapply the vendored patches in patches/ to the freeink-sdk submodule.
#
# The SDK lives in a repository we do not control, so a change we depend on
# cannot be committed there and picked up by bumping the pointer. Carrying it as
# a patch keeps a clean checkout building: without this the tree compiles only
# on a machine that happens to have the edit in its submodule working copy.
#
# Idempotent by reverse-check, so a rebuild, a resumed build, or a submodule that
# already carries the change upstream all no-op rather than conflict.
import subprocess
from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SDK_DIR = PROJECT_DIR / "freeink-sdk"
PATCH_DIR = PROJECT_DIR / "patches"


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(SDK_DIR), *args],
        capture_output=True,
        text=True,
    )


def apply_patch(patch: Path) -> None:
    name = patch.name
    # Already applied (ours, or landed upstream): the reverse would apply cleanly.
    if git("apply", "--reverse", "--check", str(patch)).returncode == 0:
        print(f"freeink-sdk: {name} already applied")
        return

    if git("apply", "--check", str(patch)).returncode != 0:
        # Neither applies nor un-applies: the submodule moved under the patch.
        # Fail loudly — a silently skipped patch becomes a confusing compile
        # error a long way from here.
        result = git("apply", "--check", str(patch))
        raise SystemExit(
            f"freeink-sdk: {name} no longer applies to the pinned submodule.\n"
            f"{result.stderr.strip()}\n"
            "If the change landed upstream, bump the submodule and delete the "
            "patch; otherwise regenerate it. See patches/README.md."
        )

    result = git("apply", str(patch))
    if result.returncode != 0:
        raise SystemExit(f"freeink-sdk: applying {name} failed\n{result.stderr.strip()}")
    print(f"freeink-sdk: applied {name}")


if not SDK_DIR.exists():
    raise SystemExit(
        "freeink-sdk submodule is missing. Run:\n"
        "  git submodule update --init --recursive"
    )

for patch_file in sorted(PATCH_DIR.glob("*.patch")):
    apply_patch(patch_file)
