#!/usr/bin/env python3
"""Check that the English and Chinese documentation stay in step.

Two kinds of drift are silent, and both have happened in this repository:

  * a page added to one tree and not the other. ``docs/index.md`` states that
    the two sets mirror each other file for file, so a page that exists on only
    one side makes that claim false.
  * an identifier translated, renamed or lost. The Chinese pages are prose
    translations; every ``ql_*`` and ``QLIB_*`` name in them must survive
    verbatim, because a transliterated function name is worse than useless.
    This is compared page by page, not across the tree as a whole: a name that
    survives somewhere is no help to a reader of the page that lost it.

Usage:
    python3 ci/check_i18n.py [repo_root]

Exit status is 1 if anything is out of step, 0 otherwise.
"""

import os
import re
import sys

# Pages that legitimately exist on one side only. This is not a way to sneak a
# shared page past the check: a page meant to exist in both trees still has to,
# and the list is deliberately tiny.
#
#   mainpage.md   Doxygen accepts a single landing page, so it stays English.
#
# development-workflow.md used to be listed here too, as a Chinese-only manual
# for the maintainer. It has an English counterpart now, so the exception went
# with it -- which is the only way an exception should ever leave this file.
ENGLISH_ONLY = {"mainpage.md"}

IDENTIFIER_RE = re.compile(r"\b(?:ql|QLIB)_[A-Za-z0-9_]+\b")

SKIP_DIR_NAMES = {"html", "latex"}


def pages(root, subdir):
    """Relative paths of every maintained Markdown page under *subdir*."""
    found = set()
    base = os.path.join(root, "docs", subdir) if subdir else os.path.join(root, "docs")
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES and not d.startswith("build-")]
        if subdir == "" and "zh" in dirnames:
            # The Chinese tree is walked separately.
            dirnames.remove("zh")
        for name in sorted(filenames):
            if name.endswith(".md"):
                found.add(os.path.relpath(os.path.join(dirpath, name), base))
    return found


def identifiers_in(path):
    with open(path, encoding="utf-8") as handle:
        return set(IDENTIFIER_RE.findall(handle.read()))


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    problems = 0

    english = pages(root, "")
    chinese = pages(root, "zh")

    for page in sorted(english - chinese - ENGLISH_ONLY):
        print(f"MISSING TRANSLATION  docs/{page} has no docs/zh/{page}", file=sys.stderr)
        problems += 1
    for page in sorted(chinese - english):
        print(f"ORPHAN TRANSLATION   docs/zh/{page} has no docs/{page}", file=sys.stderr)
        problems += 1

    every_id = set()
    for page in sorted(english & chinese):
        en_ids = identifiers_in(os.path.join(root, "docs", page))
        zh_ids = identifiers_in(os.path.join(root, "docs", "zh", page))
        every_id |= en_ids | zh_ids

        for name in sorted(en_ids - zh_ids):
            print(f"IDENTIFIER MISSING   docs/zh/{page} does not mention {name}", file=sys.stderr)
            problems += 1
        for name in sorted(zh_ids - en_ids):
            print(f"IDENTIFIER UNKNOWN   docs/zh/{page} mentions {name}, absent from docs/{page}", file=sys.stderr)
            problems += 1

    print(
        f"{len(english)} English pages, {len(chinese)} Chinese pages, "
        f"{len(every_id)} identifiers, {problems} problem(s)"
    )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
