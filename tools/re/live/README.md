# Live memory helpers

These read and write a running MCC's memory from Python (ctypes ReadProcessMemory/WriteProcessMemory). `h2mem.py` finds MCC and halo2.dll itself; for `cemem.py`, set `PID` and `BASE` at the top.

- `h2mem.py`, `cemem.py`: Halo 2 / CE readers (object, player, tag helpers).
- `scan3.py`: scans the whole process for a float pair (e.g. look yaw/pitch from the unit's aim vector) and re-checks each hit immediately, so moving the view mid-scan doesn't matter.
- `findstr.py "<text>"`: finds a string anywhere in memory (`/` matches `\`).

The method that found CE's player control: scan for the value, nudge each hit, and see which one the game keeps and follows (copies revert; the master sticks). Then search the DLL's `.data` for a pointer to it.
- `h2names.py`: tag names from a Halo 2 .map (`names('03b_newmombasa')[datum & 0xFFFF]`).
- `h2bsp.py`: the live sbsp's collision BSP and instanced geometry, and surfaces near a point (for finding what blocks you).
- `scancam.py <yaw offset> <pitch offset>`: scans for the camera's view angles when they differ from the unit's aim by a known offset.
- `ptrscan.py <hex addr> <hex span>`: pointers in module images into [addr - span, addr], to turn a heap address into a global + offset.
- `h3mem.py`: live Halo 3 reader (TLS game state, objects, tags, tag names from the .map). Process access comes from `h4mem.py`.
