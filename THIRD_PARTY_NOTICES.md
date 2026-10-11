# Third-party notices

Every third-party dependency of this project, whether fetched at build time or copied into `third_party/`, is listed here. The list is updated in the same change that adds, updates or removes a dependency (rules: `.claude/rules/third-party.md`; check: `python tools/check_notices.py`).

This project is licensed under GPL-3.0 (`LICENSE`). Each dependency's license must be GPL-3.0 compatible; see `docs/decisions/dependencies.md`.

## Format

One row per dependency:

| Name | Version / commit | License | URL | Modified | Included as |
|---|---|---|---|---|---|
| *example: SDL3* | *3.2.x tag or commit* | *zlib* | *https://…* | *no* | *FetchContent (URL_HASH SHA256)* |

- **Name**: the `FetchContent_Declare` name or the `third_party/<name>` folder name (the check compares these case-insensitively).
- **Modified**: `yes` only for copies in `third_party/` that carry local changes (listed in their `UPSTREAM.md`).
- **Included as**: `FetchContent`, `ExternalProject` or `third_party/<name>`.

## Dependencies

| Name | Version / commit | License | URL | Modified | Included as |
|---|---|---|---|---|---|

*(none yet)*
