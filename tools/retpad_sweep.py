#!/usr/bin/env python3
"""CT3-nx: function starts that follow a ret and padding but sit inside a known function.

seed_sweep.py's [ret] category only takes starts right after a ret with no padding, and its [pad] category only
starts outside every known function. When the static pass measures a function too long (CT3: its
tail_jump_alias entries), a real function after `ret; nop...` is swallowed by it and every call reaches an empty
stub: 0x00071540 (PC boot test 9: a white screen after Start) and 0x000C1BE0 were found this way.

A candidate is 16-byte aligned, preceded by padding (0x90 or 0xCC) that a real ret or ret N ends just before (the
container is disassembled from its start, so the ret is an instruction boundary, not a data byte), not a known
start or seed, and starts with a usual prologue.

Usage: python3 tools/retpad_sweep.py default.xbe functions.json [seed_functions.json]
"""
import bisect, json, struct, sys
import capstone

PRO = (b"\x55\x8b\xec", b"\x56", b"\x57", b"\x53", b"\x55", b"\x51", b"\x8b\x44\x24", b"\x8b\x4c\x24",
       b"\x8b\x54\x24", b"\x83\xec", b"\x81\xec", b"\x6a", b"\x68", b"\xa1", b"\x8b\x0d", b"\x8b\x15",
       b"\x8b\xc1", b"\x33\xc0", b"\x64\xa1", b"\xb8", b"\xd9\x44\x24")


def main():
    x = open(sys.argv[1], "rb").read()
    funcs = json.load(open(sys.argv[2]))
    seeds = json.load(open(sys.argv[3])) if len(sys.argv) > 3 else []
    base = struct.unpack("<I", x[0x104:0x108])[0]
    nsec, secaddr = struct.unpack("<II", x[0x11C:0x124])
    secs = []
    for i in range(nsec):
        o = secaddr - base + i * 56
        _, va, vs, ra, rs, _ = struct.unpack("<IIIIII", x[o:o + 24])
        secs.append((va, ra, rs))

    def raw(va, n):
        for sva, ra, rs in secs:
            if sva <= va and va + n <= sva + rs:
                return x[ra + va - sva:ra + va - sva + n]
        return b""

    known = {int(f["start"], 16) for f in funcs} | {int(s["start"], 16) for s in seeds}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    found = []
    for f in funcs:
        s, e = int(f["start"], 16), int(f["end"], 16)
        for a in range((s + 16) & ~15, e, 16):
            if a in known or not raw(a, 4).startswith(PRO):
                continue
            b = raw(a - 24, 24)
            if not b or b[-1] not in (0x90, 0xCC):
                continue
            j = len(b)
            while j and b[j - 1] in (0x90, 0xCC):
                j -= 1
            pad_start = a - (len(b) - j)
            if not any(i.address + i.size == pad_start and i.mnemonic.startswith("ret")
                       for i in md.disasm(raw(s, a - s), s)):
                continue
            first = "; ".join(f"{i.mnemonic} {i.op_str}" for i in list(md.disasm(raw(a, 12), a))[:2])
            found.append((a, f["name"], f.get("detection_method", ""), first))
    for a, name, how, first in found:
        print(f"0x{a:08X} [retpad] inside {name} ({how}); starts '{first}'")
    print(f"{len(found)} candidates", file=sys.stderr)


if __name__ == "__main__":
    main()
