"""
Script: package_readme.py
Description:
    Packs the PlatformIO library (pio pkg pack) with a README.md whose links
    work on the PlatformIO registry page.

    The registry shows the packaged README.md on its own, without the
    repository, so relative links and images go nowhere and links to a section
    of the page do nothing (its headings have no IDs). The README in the
    repository keeps relative links, which work on GitHub, in forks and branches,
    and in a local clone. This script rewrites them, for the package only, to
    GitHub URLs pinned to the release tag, so the registry page links to the
    docs of the version it shows:

        docs/BUILD.md#with-cmake  ->  https://github.com/.../blob/vX.Y.Z/docs/BUILD.md#with-cmake
        example/sample-projects/  ->  https://github.com/.../tree/vX.Y.Z/example/sample-projects/
        docs/LayerDiagram.svg     ->  https://raw.githubusercontent.com/.../vX.Y.Z/docs/LayerDiagram.svg (images)
        #platform-support         ->  https://github.com/.../blob/vX.Y.Z/README.md#platform-support

    It writes that README.md, runs pio pkg pack in the repository root, puts
    the original README.md back (even if packing fails), and checks the
    packed README.md has no relative link left.

Usage (release: after tagging vX.Y.Z, from the repository root):
    > python scripts/package_readme.py               # tag v<library.json version>
    > python scripts/package_readme.py --ref master  # try it before tagging
    > python scripts/package_readme.py --print       # show the rewritten README, don't pack
    Then publish the tarball it made, so the registry gets exactly that:
    > pio pkg publish DelegateMQ-X.Y.Z.tar.gz --type library --no-interactive
"""

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GITHUB = "https://github.com/DelegateMQ/DelegateMQ"
RAW = "https://raw.githubusercontent.com/DelegateMQ/DelegateMQ"

# Markdown links and images: [text](target) and ![alt](target), with an
# optional "title". HTML: src="target" and href="target".
MD_LINK = re.compile(r'(!?)\[([^\]]*)\]\(\s*([^)\s]+)((?:\s+"[^"]*")?)\s*\)')
HTML_ATTR = re.compile(r'\b(src|href)="([^"]+)"')
ABSOLUTE = re.compile(r"^(?:[a-z][a-z0-9+.-]*:|//)", re.IGNORECASE)   # http:, https:, mailto:, //host


def absolute_url(target, ref, image):
    """The URL a relative README target becomes in the package."""
    if ABSOLUTE.match(target):
        return target
    if target.startswith("#"):
        return f"{GITHUB}/blob/{ref}/README.md{target}"
    path, _, anchor = target.partition("#")
    path = path[2:] if path.startswith("./") else path
    if image:
        url = f"{RAW}/{ref}/{path}"
    elif os.path.isdir(os.path.join(REPO_ROOT, path)):
        url = f"{GITHUB}/tree/{ref}/{path}"
    else:
        url = f"{GITHUB}/blob/{ref}/{path}"
    return url + (f"#{anchor}" if anchor else "")


FENCE = re.compile(r"^[ \t]*```.*?^[ \t]*```[ \t]*$", re.MULTILINE | re.DOTALL)
INLINE_CODE = re.compile(r"`[^`\n]*`")


def rewrite(text, ref):
    """README text with every relative link and image made absolute. Code is
    left alone: fenced blocks and inline code spans (C++ like [](int x) isn't
    a link)."""
    def md(m):
        bang, label, target, title = m.groups()
        return f"{bang}[{label}]({absolute_url(target, ref, image=bool(bang))}{title})"

    def html(m):
        attr, target = m.groups()
        return f'{attr}="{absolute_url(target, ref, image=(attr == "src"))}"'

    def prose(chunk):
        # Set inline code aside while rewriting, so code that looks like a link
        # isn't touched but a link labelled with code ([`x`](path)) still is
        spans = []

        def hide(m):
            spans.append(m.group(0))
            return f"\x00{len(spans) - 1}\x00"

        hidden = INLINE_CODE.sub(hide, chunk)
        hidden = HTML_ATTR.sub(html, MD_LINK.sub(md, hidden))
        return re.sub(r"\x00(\d+)\x00", lambda m: spans[int(m.group(1))], hidden)

    out, pos = [], 0
    for m in FENCE.finditer(text):
        out.append(prose(text[pos:m.start()]))
        out.append(m.group(0))
        pos = m.end()
    out.append(prose(text[pos:]))
    return "".join(out)


def relative_targets(text):
    """Link targets outside code that aren't absolute URLs (should be none after rewrite)."""
    text = INLINE_CODE.sub("x", FENCE.sub("", text))   # keeps [`label`](link) a link
    targets = [m.group(3) for m in MD_LINK.finditer(text)] + [m.group(2) for m in HTML_ATTR.finditer(text)]
    return [t for t in targets if not ABSOLUTE.match(t)]


def find_pio():
    exe = "pio.exe" if os.name == "nt" else "pio"
    home_pio = os.path.join(os.path.expanduser("~"), ".platformio", "penv",
                            "Scripts" if os.name == "nt" else "bin", exe)
    return shutil.which("pio") or (home_pio if os.path.isfile(home_pio) else None)


def main():
    with open(os.path.join(REPO_ROOT, "library.json"), encoding="utf-8") as f:
        version = json.load(f)["version"]
    parser = argparse.ArgumentParser(description="Pack the PlatformIO library with registry-ready README links.")
    parser.add_argument("--ref", default=f"v{version}",
                        help=f"git tag or branch the links point to (default v{version}, from library.json)")
    parser.add_argument("--print", action="store_true", help="print the rewritten README.md and exit; don't pack")
    args = parser.parse_args()

    readme = os.path.join(REPO_ROOT, "README.md")
    with open(readme, "rb") as f:
        original = f.read()
    packaged = rewrite(original.decode("utf-8"), args.ref)

    if args.print:
        sys.stdout.buffer.write(packaged.encode("utf-8"))   # UTF-8 whatever the console's code page
        return 0

    tag_check = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"refs/tags/{args.ref}"],
                               cwd=REPO_ROOT, capture_output=True)
    if tag_check.returncode != 0:
        print(f"Note: tag {args.ref} doesn't exist (yet); the registry links 404 until it's pushed.")

    pio = find_pio()
    if not pio:
        print("ERROR: pio not found (PlatformIO Core)")
        return 1

    before = set(glob.glob(os.path.join(REPO_ROOT, "*.tar.gz")))
    try:
        with open(readme, "wb") as f:
            f.write(packaged.encode("utf-8"))
        result = subprocess.run([pio, "pkg", "pack"], cwd=REPO_ROOT)
    finally:
        with open(readme, "wb") as f:
            f.write(original)          # the repository README, byte for byte
    if result.returncode != 0:
        print("ERROR: pio pkg pack failed")
        return 1

    made = sorted(set(glob.glob(os.path.join(REPO_ROOT, "*.tar.gz"))) - before) \
        or sorted(glob.glob(os.path.join(REPO_ROOT, "*.tar.gz")), key=os.path.getmtime)[-1:]
    if not made:
        print("ERROR: no tarball found after packing")
        return 1
    tarball = made[0]
    with tarfile.open(tarball, "r:gz") as tar:
        member = next((m for m in tar.getmembers() if os.path.basename(m.name) == "README.md"
                       and m.name.count("/") <= 1), None)
        if member is None:
            print(f"ERROR: {os.path.basename(tarball)} has no README.md")
            return 1
        packed = tar.extractfile(member).read().decode("utf-8")
    left = relative_targets(packed)
    if left:
        print(f"ERROR: the packed README.md still has relative links: {left[:5]}")
        return 1
    print(f"Packed {os.path.basename(tarball)}: README.md links point to {args.ref}.")
    print(f"Publish it with: pio pkg publish {os.path.basename(tarball)} --type library --no-interactive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
