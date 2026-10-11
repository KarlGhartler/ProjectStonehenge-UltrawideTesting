#!/usr/bin/env python3
"""Check third-party bookkeeping (rules: .claude/rules/third-party.md).

Usage:
    python tools/check_notices.py [repo_root]

Fails (exit 1) if:
  - a folder in third_party/ lacks a license file (LICENSE*, COPYING*, LICENCE*) or UPSTREAM.md,
  - a dependency declared in CMake (FetchContent_Declare / ExternalProject_Add) or present in
    third_party/ has no row in THIRD_PARTY_NOTICES.md ("## Dependencies" table),
  - a FetchContent/ExternalProject declaration is not pinned: an archive URL needs URL_HASH, a git
    repository needs GIT_TAG with a full 40-hex commit.
Prints a warning (no failure) for notices rows that match no dependency.
"""
import os
import re
import sys

LICENSE_NAMES = ("license", "licence", "copying")
DECL_RE = re.compile(r"\b(FetchContent_Declare|ExternalProject_Add)\s*\(\s*([A-Za-z0-9_.+-]+)(.*?)\)", re.S | re.I)


def cmake_files(root):
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root).replace("\\", "/")
        # Skip build output, VCS data and third-party trees (their own CMake files are not ours).
        if rel.split("/")[0] in (".git", "build", "out", "third_party", "_deps"):
            dirnames[:] = []
            continue
        for f in filenames:
            if f == "CMakeLists.txt" or f.endswith(".cmake"):
                yield os.path.join(dirpath, f)


def cmake_deps(root):
    deps = {}
    for path in cmake_files(root):
        text = open(path, encoding="utf-8", errors="replace").read()
        text = re.sub(r"#[^\n]*", "", text)  # strip comments
        for m in DECL_RE.finditer(text):
            deps[m.group(2).lower()] = (os.path.relpath(path, root), m.group(3))
    return deps


def pin_problem(body):
    has_url = re.search(r"\bURL\s+\S+", body, re.I)
    has_git = re.search(r"\bGIT_REPOSITORY\s+\S+", body, re.I)
    if has_url and not re.search(r"\bURL_HASH\s+(SHA256|SHA512|SHA1|MD5)=[0-9A-Fa-f]+", body, re.I):
        return "URL without URL_HASH"
    if has_git:
        tag = re.search(r"\bGIT_TAG\s+([0-9A-Za-z._/-]+)", body, re.I)
        if not tag or not re.fullmatch(r"[0-9a-fA-F]{40}", tag.group(1)):
            return "GIT_TAG is not a full 40-hex commit"
    if not has_url and not has_git:
        return "no URL or GIT_REPOSITORY found"
    return None


def notices_names(root):
    path = os.path.join(root, "THIRD_PARTY_NOTICES.md")
    if not os.path.exists(path):
        return None
    names, in_deps = set(), False
    for line in open(path, encoding="utf-8"):
        if line.startswith("## "):
            in_deps = line.strip().lower() == "## dependencies"
            continue
        if in_deps and line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if not cells or cells[0].lower() in ("name", "") or set(cells[0]) <= set("-: "):
                continue
            names.add(cells[0].strip("*` ").lower())
    return names


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    problems = []
    notices = notices_names(root)
    if notices is None:
        problems.append("THIRD_PARTY_NOTICES.md is missing")
        notices = set()

    deps = {}
    tp = os.path.join(root, "third_party")
    if os.path.isdir(tp):
        for name in sorted(os.listdir(tp)):
            d = os.path.join(tp, name)
            if not os.path.isdir(d):
                continue
            files = [f.lower() for f in os.listdir(d)]
            if not any(f.startswith(LICENSE_NAMES) for f in files):
                problems.append(f"third_party/{name}: no LICENSE/COPYING file")
            if "upstream.md" not in files:
                problems.append(f"third_party/{name}: no UPSTREAM.md")
            deps[name.lower()] = f"third_party/{name}"

    for name, (path, body) in cmake_deps(root).items():
        deps.setdefault(name, path)
        why = pin_problem(body)
        if why:
            problems.append(f"{path}: dependency '{name}' is not pinned ({why})")

    for name, where in sorted(deps.items()):
        if name not in notices:
            problems.append(f"{where}: dependency '{name}' has no entry in THIRD_PARTY_NOTICES.md")
    for name in sorted(notices - set(deps)):
        print(f"warning: THIRD_PARTY_NOTICES.md lists '{name}', but no dependency with that name was found")

    for p in problems:
        print("error:", p)
    print(f"check_notices: {len(deps)} dependencies, {len(notices)} notices entries, {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
