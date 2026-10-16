#!/usr/bin/env python3
"""Check that every relative link in the documentation resolves.

With the docs maintained in two languages and cross-linked in both directions,
a renamed page or a wrong number of "../" segments is easy to miss in review and
invisible until someone clicks it. This turns that into a CI failure.

Usage:
    python3 ci/check_links.py [repo_root]

Exit status is 1 if any link is broken, 0 otherwise.
"""

import os
import re
import sys

# [text](target) -- the only link form the docs use. Bare URLs, anchors and
# mailto: links are skipped.
LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")

# Generated trees and anything that is not documentation we maintain.
SKIP_DIRS = {"build", ".git", "__pycache__"}
SKIP_DIR_NAMES = {"html", "latex"}


def markdown_files(root):
    """Yield every Markdown file we are responsible for."""
    for dirpath, dirnames, filenames in os.walk(os.path.join(root, "docs")):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES and not d.startswith("build-")]
        for name in sorted(filenames):
            if name.endswith(".md"):
                yield os.path.join(dirpath, name)

    for name in ("README.md", "CONTRIBUTING.md"):
        path = os.path.join(root, name)
        if os.path.isfile(path):
            yield path


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    checked = 0
    broken = []

    for path in sorted(markdown_files(root)):
        if any(part in SKIP_DIRS for part in path.split(os.sep)):
            continue

        with open(path, encoding="utf-8") as handle:
            text = handle.read()

        for match in LINK_RE.finditer(text):
            target = match.group(1)
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = target.split("#", 1)[0]
            if not target:
                continue

            checked += 1
            resolved = os.path.normpath(os.path.join(os.path.dirname(path), target))
            if not os.path.exists(resolved):
                broken.append((os.path.relpath(path, root), match.group(1), os.path.relpath(resolved, root)))

    for source, link, resolved in broken:
        print(f"BROKEN  {source}: {link}  ->  {resolved}", file=sys.stderr)

    print(f"{checked} relative links checked, {len(broken)} broken")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
