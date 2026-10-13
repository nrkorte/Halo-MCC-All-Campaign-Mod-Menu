# Reverse-engineering helpers

These are small Python scripts (capstone + pefile) used to find the Halo 3 offsets in `src/games/halo3/offsets.h`. Install the dependencies with `pip install capstone pefile`.

## Setup

1. Put Assembly's `H3MCC_U14_Scripting.xml` in `../h3/` (relative to these scripts). It comes from github.com/XboxChaos/Assembly, at `src/Blamite/Formats/Halo3MCC/`.
2. Run `python hs_table.py`. It writes `halo3.img`, a memory-mapped image of `halo3.dll`. Check the DLL path in the script first.
3. Run `python scan.py`. It disassembles `.text` to `text.pkl` (about 1.9 million instructions, takes about a minute). It expects `halo3.img` in the current folder.

## Tools

| Script | Use |
|---|---|
| `hsfunc.py` | `from hsfunc import *`, then `defn(opcode)` and `dis(rva)`. HaloScript function table at RVA 0x7F6AD0; the evaluator is at definition+0x20 |
| `around.py <rva> <before> <after>` | Disassembly around an address |
| `grep_asm.py '<regex>' [context] [limit]` | Regex search over all instructions |
| `riprefs.py <rva,rva> [w]` | Code that references a global through RIP-relative addressing (`w` = writes only) |
| `tagreads.py <offset>...` | Reads of a field that come right after loading the tag base, i.e. reads of tag data |
| `callchain.py <rva>` | Walks callers upward |

The workflow and key findings are in `NOTES.md` and in the offsets file comments.
