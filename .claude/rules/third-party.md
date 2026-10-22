---
paths:
  - "third_party/**/*"
  - "CMakeLists.txt"
  - "cmake/**/*"
  - "THIRD_PARTY_NOTICES.md"
---

# Rules for third-party code and dependencies

## Approval
- Every new dependency needs the User's approval **and** a GPL-3.0 compatibility check (record the license and the result in `docs/decisions/dependencies.md`) before it is added, fetched or downloaded.

## Unmodified dependencies
- Fetched by CMake `FetchContent` (or `ExternalProject`), pinned to an exact version or commit **with a checksum**:
  - archives: `URL` + `URL_HASH SHA256=…`;
  - git: `GIT_TAG <full 40-hex commit>` (no branches or moving tags).
- Never copied into the repo.

## Modified dependencies (only code we change)
- Copied into `third_party/<name>/`, which must contain:
  - the original license file (`LICENSE`, `COPYING` or the upstream equivalent, unchanged);
  - `UPSTREAM.md`: source URL, exact version/commit, license, and the list of local changes;
  - the original copyright headers in every file, kept as they are;
  - in every file we changed, a note: `Modified by the ProjectStonehenge project` (with a short reason, next to the original header).
- Never edit third-party code outside `third_party/`. Never copy third-party code into `src/`.

## Notices
- `THIRD_PARTY_NOTICES.md` lists every dependency, fetched or copied: name, version/commit, license, URL, modified (yes/no), how included.
- Update it in the **same change** that adds, updates or removes a dependency.
- `python tools/check_notices.py` must pass. It fails if:
  - a `third_party/` folder lacks a license file or `UPSTREAM.md`;
  - a dependency in CMake or `third_party/` has no notices entry;
  - a `FetchContent` declaration is not pinned with a checksum or full commit.

## Git hook
- `tools/hooks/pre-commit` exempts `third_party/` from the 1 MiB size limit, but never from the game-data name and signature checks.
