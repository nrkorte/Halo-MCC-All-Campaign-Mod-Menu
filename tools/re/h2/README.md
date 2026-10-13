# Halo 2 reverse-engineering helpers

For `halo2.dll` (MCC 1.3528). Run `python mkimg.py` first: it writes `h2.img` and `text.pkl` from `halo2.dll` (`.text` at 0x1000, size 0xB04893; check the DLL path in the script).

- `ar.py <rva> <before> <after>`: disassembly around an address; `ar.py callers <rva>...` lists calls/jumps to a function.

- `hs.py`: HaloScript function table (RVA 0xE04850), indexed by the names in the binary; evaluators at definition +0x20.
- `deep.py <name>...`: an evaluator plus its callees, showing only field accesses (quick way to read an offset).
- `show.py`, `aob.py` and `plug.py` work as in `../reach/`.

Halo 2's .map files are zlib-compressed in 0x40000-byte chunks (chunk table at header +0x310). Header file offsets minus 0x380 index the decompressed data.
