"""Resolve a README.md merge conflict with upstream: this fork's block on top of upstream's new README.

The fork's README.md is its own block followed by upstream's README unchanged, so a merge conflicts there whenever
upstream edits the lines next to the block (its title, on every release). Run during a conflicted `git merge`: it
takes the block from our side, appends their README and stages the result. It refuses when our side no longer ends
with the merge base's README, i.e. when upstream's part was edited here.
"""
import subprocess
import sys


def stage(number: int) -> bytes:
    return subprocess.run(["git", "show", f":{number}:README.md"], capture_output=True, check=True).stdout


def main() -> int:
    base, ours, theirs = stage(1), stage(2), stage(3)
    if not ours.endswith(base):
        print("README.md: our side does not end with upstream's previous README; resolve it by hand", file=sys.stderr)
        return 1
    block = ours[: len(ours) - len(base)]
    with open("README.md", "wb") as output:
        output.write(block + theirs)
    subprocess.run(["git", "add", "README.md"], check=True)
    print(f"README.md: kept the fork block ({len(block)} bytes) above upstream's new README")
    return 0


if __name__ == "__main__":
    sys.exit(main())
