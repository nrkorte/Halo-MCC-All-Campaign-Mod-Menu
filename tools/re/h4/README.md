# Halo 4 reverse-engineering helpers

For `halo4.dll` (MCC 1.3528). What was found with these is in `src/games/halo4/offsets.h`.

## Setup

1. Assembly's Halo4MCC scripting XML and plugins are in `xml/` (from github.com/XboxChaos/Assembly, `src/Blamite/Formats/Halo4MCC/` and `src/Assembly/Plugins/Halo4MCC/`).
2. `python mkimg.py` writes `h4.img` (halo4.dll mapped at its section RVAs) and `text.pkl` (.text disassembly). `../around.py`, `../grep_asm.py` and `../riprefs.py` work on these unchanged.

## The script table is built at runtime

Unlike Reach and Halo 3, Halo 4's HaloScript function definitions aren't in the DLL: they're C++ objects allocated at startup (vtable +0, return type +8, name +0x10, help +0x18, evaluator +0x60; vtable[0] calls `[this+0x60]`). `hsdump.py` scans a running MCC with Halo 4 loaded for them and writes `hs_funcs.txt` (name, evaluator RVA, object address): 1,591 of the 2,373 names in the XML (the rest are debug functions with no object). The globals table is static (RVA 0xD462E0: pointers to {name, type, address}).

## Tools

| Script | Use |
|---|---|
| `hsdump.py` | Rebuilds `hs_funcs.txt` from the running game (needs `h4live.img`, a dump of the loaded module: `h4mem.dump('h4live.img')`) |
| `show.py <rva>[:lines]` | Disassembles at an RVA |
| `fn.py <rva>` | The function containing an RVA, from `.pdata` |
| `xref.py <rva>` | Direct call/jmp sites to an RVA (raw byte scan) |
| `aob.py "<pattern>"` | Where a byte pattern matches (Cheat Engine patterns) |
| `plug.py <plugin.xml> [filter]` | Flattens an Assembly plugin into offsets |
| `port.py <reach rva>` | Tries to find Reach code in Halo 4 by masked byte pattern (rarely unique; the code generation differs) |
| `objs.py <types>` | Lists live objects of the given types |

`../live/h4mem.py` reads and writes the running game: TLS game state (`state()`, `slot(n)`), objects, tags (`tag_get`, `block`, `tags_of('weap')`), tag names from the .map file, and `search()` over the whole process. Halo 4 loads at its preferred base (0x180000000), so live addresses equal 0x180000000 + RVA.
