---
paths:
  - "src/platform/render/**/*"
  - "docs/subsystems/vu-programs.md"
---

# Rules for VU microcode

- `.vutext`, `.vudata` and the `.DVP.overlay.*` sections hold Vector Unit programs: 64-bit upper/lower instruction pairs, not R5900 code. Never analyse them with R5900 tools.
- For each microprogram, first document in `docs/subsystems/vu-programs.md` (one section per program): which EE function uploads it (VIF `MPG` command), its input data layout, its output (GS packets), and the overlay section it comes from.
- Reimplement each program's effect (transform, lighting, clipping, packet output) on the CPU or as a GPU shader. Do not write a VU instruction emulator unless asked.
- Reference data comes from the PCSX2 debugger: dump VU memory and registers before and after a program runs.
