# Supported game version

The port supports exactly one disc image: the user's own copy of the original NTSC-U disc, as an ISO 9660 image (2048-byte sectors). The port verifies the image at startup and refuses to run on anything else. A clear error message names the expected version.

## Reference image (computed from the project owner's ISO)

| Property | Value |
|---|---|
| Title | Ace Combat 04: Shattered Skies |
| Region / serial | NTSC-U, `SLUS-20152` (`SLUS_201.52`) |
| Image format | ISO 9660, 2048-byte user-data sectors (no raw 2352-byte sectors) |
| Size | 3,554,705,408 bytes = 1,735,696 sectors |
| **SHA-1 (whole image)** | **`aaa707214aea7f8f9c2904c4b5e00c372c943533`** |
| MD5 (whole image) | `8c7cbadb78b2eebeabbf546e0c1d0372` |
| Primary volume descriptor | system id `PLAYSTATION`, volume id `ACECOMBAT04`, volume space 1,735,696 blocks, created `2001-09-02 04:00:00` |
| SHA-1 of `SLUS_201.52` | `4ac3e7ad5ee79d318c134e12484115de059f4bf8` |
| SHA-1 of `SYSTEM.CNF` | `8f3a29ea1a854311f8e80b0317529d61a9081ef3` |

How these were computed: `sha1sum`/`md5sum` over the whole ISO file, and over the files extracted from it. The volume descriptor is sector 16 of the image.

## Startup check (planned behaviour)

1. Read the ISO path from configuration. There is no default path and no searching of the disk.
2. Fast check: file size = 3,554,705,408 and primary volume descriptor ids match. This gives an immediate clear error for wrong images.
3. Full check: SHA-1 of the whole image = the value above. It reads 3.3 GB once, so cache the result keyed on path + size + modification time, and offer a "verify again" option.
4. Optional extra check: SHA-1 of `SLUS_201.52` inside the image. The port loads the executable's initialised data (`.data`, `.rodata`, `.vutext`, `.vudata`) from it at runtime, so this hash protects the translated code's assumptions about addresses.

Images that differ only in raw sector format (e.g. `.bin` 2352-byte dumps) are not supported in the first version. Converting to ISO is the user's job.

## Notes

- The hash identifies the image; it is not a secret and not copyrighted data. No game data is stored in the repo.
- Cross-check against an independent disc-preservation database (e.g. Redump) is still to do. Record the result here.
