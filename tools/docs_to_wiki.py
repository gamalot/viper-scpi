#!/usr/bin/env python3
"""Render docs/*.md as GitHub wiki pages.

The guides in docs/ are the source of truth (they are versioned with the code);
the wiki is a copy for browsing. Usage:

    python3 tools/docs_to_wiki.py <wiki-checkout-dir>
    cd <wiki-checkout-dir> && git add -A && git commit -m "Sync from docs/" && git push

Page names, links between guides, links into the repository and the
per-page "Documentation home" line are rewritten for the wiki.
"""
import os
import re
import subprocess
import sys

REPO_URL = "https://github.com/gamalot/viper-scpi"

# docs file -> wiki page name, in sidebar order
PAGES = [
    ("README.md", "Home", "Home"),
    ("getting-started.md", "Getting-Started", "Getting started"),
    ("command-tree.md", "Command-Tree", "Command tree"),
    ("parameters.md", "Parameters-and-Responses", "Parameters and responses"),
    ("transports.md", "Transports", "Transports"),
    ("status-and-errors.md", "Status-and-Errors", "Status and errors"),
    ("overlapped-commands.md", "Overlapped-Commands", "Overlapped commands"),
    ("large-data.md", "Large-Data", "Large data"),
    ("configuration.md", "Configuration-and-Footprint", "Configuration and footprint"),
    ("troubleshooting.md", "Troubleshooting", "Troubleshooting"),
    ("contributing.md", "Contributing", "Contributing"),
]
WIKI_NAME = {src: name for src, name, _ in PAGES}


def rewrite_link(target, ref):
    if re.match(r"^[a-z]+:", target) or target.startswith("#"):
        return target
    path, _, anchor = target.partition("#")
    suffix = "#" + anchor if anchor else ""
    if path in WIKI_NAME:
        return WIKI_NAME[path] + suffix
    # anything else is a file in the repository, relative to docs/
    repo_path = os.path.normpath(os.path.join("docs", path)).replace(os.sep, "/")
    kind = "tree" if path.endswith("/") or os.path.isdir(repo_path) else "blob"
    return f"{REPO_URL}/{kind}/{ref}/{repo_path}{suffix}"


def convert(text, ref):
    text = re.sub(r"^\[Documentation home\]\(README\.md\)\n\n", "", text, flags=re.M)
    return re.sub(r"\]\(([^)\s]+)\)", lambda m: "](" + rewrite_link(m.group(1), ref) + ")", text)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sha = subprocess.run(["git", "-C", root, "rev-parse", "--short", "HEAD"],
                         capture_output=True, text=True, check=True).stdout.strip()

    for src, name, _ in PAGES:
        with open(os.path.join(root, "docs", src), encoding="utf-8") as f:
            text = convert(f.read(), "main")
        if src == "README.md":
            text = text.replace("# viper-scpi documentation", "# viper-scpi", 1)
        with open(os.path.join(out, name + ".md"), "w", encoding="utf-8") as f:
            f.write(text)

    sidebar = ["**[viper-scpi](Home)**", ""]
    sidebar += [f"- [{title}]({name})" for _, name, title in PAGES[1:]]
    sidebar += ["", f"[Repository]({REPO_URL}) · [Changelog]({REPO_URL}/blob/main/CHANGELOG.md)"]
    with open(os.path.join(out, "_Sidebar.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(sidebar) + "\n")
    with open(os.path.join(out, "_Footer.md"), "w", encoding="utf-8") as f:
        f.write(f"Generated from [`docs/`]({REPO_URL}/tree/main/docs) at `{sha}`. "
                "Edit the files there, not this wiki.\n")


if __name__ == "__main__":
    main()
