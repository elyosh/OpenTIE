# Nuked-OPL3-fast provenance

- Repository: <https://github.com/tgies/Nuked-OPL3-fast>
- Fork version: `1.8-fast.3`
- Vendored commit: `f44bacb1143cd78d36bacf69fbec8c60a125b3c9`
- Upstream Nuked-OPL3 base: `cfedb09`
- Import date: 2026-08-07
- Local modifications: none

The vendored code consists of `opl3.c`, `opl3.h`, and `wf_rom.h`. `LICENSE`
is the unmodified upstream LGPL-2.1 license file.

## Update procedure

1. Check out the intended upstream commit in a clean Nuked-OPL3-fast tree.
2. Replace `opl3.c`, `opl3.h`, `wf_rom.h`, and `LICENSE` with the upstream
   versions without local edits.
3. Update the version, commit, base commit, import date, and modification status
   above.
4. Compare the imported files with the upstream commit and verify there are no
   unrecorded differences.
5. Build and run the FM4 register-trace, audio-hash, multi-instance, and
   long-running audio tests before accepting the update.

Project compile definitions and the `nuked_opl3_fast` CMake target are declared
outside this directory so the vendored snapshot remains unchanged.
