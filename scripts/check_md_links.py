"""
Script: check_md_links.py
Description:
    Checks the links in the repository's Markdown files, so they work both on
    GitHub and in a local clone (VS Code's Markdown preview included). Reports:

      absolute   a link to this repository written as a GitHub URL
                 (github.com/DelegateMQ/DelegateMQ/blob/..., /tree/..., #section,
                 raw.githubusercontent.com/...): use a relative path, or #section.
                 scripts/package_readme.py makes them absolute for the PlatformIO
                 package only.
      missing    a relative link whose file or folder doesn't exist
      folder     a link to a folder: GitHub lists it, but an editor's preview
                 can't open it. Link to a file in it (its README.md).
      anchor     a #section link with no matching heading in the target file
                 (heading IDs as GitHub makes them)

    Code (fenced blocks and inline `code`) is skipped. Exits 1 if anything is
    reported.

Usage:
    > python scripts/check_md_links.py                 # every tracked .md file
    > python scripts/check_md_links.py README.md docs/BUILD.md
"""

import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THIS_REPO = re.compile(
    r"^https?://(?:github\.com/DelegateMQ/DelegateMQ(?:/(?:blob|tree|raw)/[^/]+/|/?(?=#)|/?$)"
    r"|raw\.githubusercontent\.com/DelegateMQ/DelegateMQ/[^/]+/)(.*)$", re.IGNORECASE)
MD_LINK = re.compile(r'!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')
HTML_ATTR = re.compile(r'\b(?:src|href)="([^"]+)"')
FENCE = re.compile(r"^[ \t]*```.*?^[ \t]*```[ \t]*$", re.MULTILINE | re.DOTALL)
INLINE_CODE = re.compile(r"`[^`\n]*`")
SCHEME = re.compile(r"^[a-z][a-z0-9+.-]*:", re.IGNORECASE)


def strip_code(text):
    """text with fenced blocks and inline code blanked out, line numbers kept."""
    text = FENCE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    # Blank inline code with letters, not spaces, so a link labelled with code
    # ([`x`](path)) still reads as a link
    return INLINE_CODE.sub(lambda m: "x" * len(m.group(0)), text)


def heading_ids(path, cache={}):
    """The IDs GitHub gives the headings of a Markdown file (and HTML anchors)."""
    if path in cache:
        return cache[path]
    ids, seen = set(), {}
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        cache[path] = ids
        return ids
    for line in FENCE.sub("", text).splitlines():
        m = re.match(r"^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$", line)
        if m:
            slug = m.group(1).lower()
            slug = re.sub(r"<[^>]+>", "", slug)                   # HTML tags
            slug = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", slug)  # [text](link) -> text
            slug = re.sub(r"[^\w\- ]", "", slug)                  # punctuation, backticks
            slug = slug.replace(" ", "-")
            n = seen.get(slug, 0)
            seen[slug] = n + 1
            ids.add(slug if n == 0 else f"{slug}-{n}")
    ids.update(re.findall(r'<a\s+(?:name|id)="([^"]+)"', text))
    cache[path] = ids
    return ids


def check(md_path):
    """Problems in one Markdown file: list of (line, kind, target, message)."""
    problems = []
    text = strip_code(open(md_path, encoding="utf-8").read())
    base = os.path.dirname(md_path)
    for lineno, line in enumerate(text.splitlines(), 1):
        for target in MD_LINK.findall(line) + HTML_ATTR.findall(line):
            m = THIS_REPO.match(target)
            if m:
                rest = m.group(1)
                hint = rest if rest else "(the README itself)"
                problems.append((lineno, "absolute", target, f"link to this repository; use a relative link: {hint}"))
                continue
            if SCHEME.match(target) or target.startswith("//"):
                continue                                        # another site
            path, _, anchor = target.partition("#")
            dest = os.path.normpath(os.path.join(base, path)) if path else md_path
            if path and not os.path.exists(dest):
                problems.append((lineno, "missing", target, "no such file or folder"))
                continue
            if os.path.isdir(dest):
                readme = os.path.join(dest, "README.md")
                hint = f" (link {os.path.relpath(readme, base).replace(os.sep, '/')})" if os.path.isfile(readme) else ""
                problems.append((lineno, "folder", target, "a folder: editors can't open it" + hint))
                continue
            if anchor and dest.lower().endswith(".md") and anchor not in heading_ids(dest):
                problems.append((lineno, "anchor", target, f"no heading #{anchor} in {os.path.relpath(dest, REPO_ROOT)}"))
    return problems


def main():
    os.chdir(REPO_ROOT)
    files = sys.argv[1:] or [f for f in subprocess.run(
        ["git", "ls-files", "*.md"], capture_output=True, text=True).stdout.splitlines() if f]
    total = 0
    for f in files:
        for lineno, kind, target, message in check(f):
            print(f"{f}:{lineno}: {kind:8} {target}  -- {message}")
            total += 1
    print(f"{total} problem(s) in {len(files)} file(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
