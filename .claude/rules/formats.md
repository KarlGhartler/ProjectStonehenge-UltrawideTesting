---
paths:
  - "tools/**/*"
  - "docs/formats/**/*"
  - "src/game/data/**/*"
---

# Rules for data formats and tools

- Derive every format layout from the game's loader code. Cite the EE address of the parsing function.
- Each format gets `docs/formats/<NAME>.md` with: magic/identifier, header fields, per-field offset/type/meaning, parsing function address, and confidence (confirmed / hypothesis).
- Tools take the extracted-disc folder as a command-line argument, never write into the repo except `docs/`, and write their output to `out/`.
- Tools are research aids only: the port parses every format from the ISO at runtime. Never commit tool output (extracted or converted assets) to the repo.
