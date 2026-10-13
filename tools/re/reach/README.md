# Reach reverse-engineering helpers

Same approach as the Halo 3 scripts one folder up, for `haloreach.dll` (MCC 1.3528).

## Setup

1. Put Assembly's `ReachMCC_U13_Scripting.xml` and the ReachMCC plugins you need (`weap.xml`, `matg.xml`, ...) in `xml/` here. They come from github.com/XboxChaos/Assembly (`src/Blamite/Formats/ReachMCC/`, `src/Assembly/Plugins/ReachMCC/`).
2. Run `python mkimg.py` to write `reach.img` (a memory-mapped image of `haloreach.dll`) and `text.pkl` (disassembled `.text`). Check the DLL path in the script.
3. `../around.py`, `../grep_asm.py`, `../riprefs.py` and `../callchain.py` work unchanged on these files.

## Tools

| Script | Use |
|---|---|
| `hsfind.py` | Finds the HaloScript function table (RVA 0xA28D40 in 1.3528) |
| `hs.py` | `python hs.py <name>` prints a script function's definition or a global's address. In Python: `evaluator(name)`, `ev(name)` |
| `show.py <name or rva>[:lines]` | Disassembles a script evaluator or function |
| `tls.py <name>` | Rough list of TLS slots a script function reads (check by reading the code) |
| `aob.py "<pattern>"...` | Where a byte pattern matches (to check Cheat Engine patterns are unique) |
| `plug.py <plugin.xml> [filter]` | Flattens an Assembly plugin into offsets |
| `mapnames.py <text> <map>...` | Offline: tag names containing <text> in .map files (e.g. `pelican m20 m52`) |
| `dz.py` | Live: the scenario's designer zones (what each loads) and zone sets (which designer zones each allows), plus the active mask |

What was found with these is in `src/games/reach/offsets.h`, with the RVA each value was read from.
