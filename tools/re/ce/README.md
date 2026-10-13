# Halo CE reverse-engineering helpers

For `halo1.dll` (MCC 1.3528). Same setup as `../reach/`: write `ce.img` and `text.pkl` from `halo1.dll` (`.text` at 0x1000, size 0x174F171), and put Assembly's Halo1/Halo1MCC plugins in `xml/`.

- `hs.py`: the HaloScript function table (RVA 0x18960F0). It's indexed by the names stored in the binary, because Assembly's `H1AMCC_U1_Scripting.xml` only lines up for the first 343 functions. Evaluators are at definition +0x18.
- `show.py`, `aob.py` and `plug.py` work as in `../reach/` (`aob.py` scans the whole `.text`).

Notes: CE's game state is in plain globals (no TLS), object fields are the classic 32-bit CE layout minus 0x44, and the `cheat_*` script globals are real switches the engine reads. Everything found is in `src/games/halo1/offsets.h`.
