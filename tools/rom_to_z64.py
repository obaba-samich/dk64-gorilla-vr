#!/usr/bin/env python3
"""Write a big-endian (.z64) copy of an N64 ROM. The input is never modified."""
import sys


def main():
    if len(sys.argv) != 3:
        print("usage: rom_to_z64.py <in.z64|v64|n64> <out.z64>", file=sys.stderr)
        return 2
    data = bytearray(open(sys.argv[1], "rb").read())
    magic = bytes(data[:4])
    if magic == b"\x80\x37\x12\x40":
        pass
    elif magic == b"\x37\x80\x40\x12":  # .v64, 16-bit swapped
        data[0::2], data[1::2] = data[1::2], data[0::2]
    elif magic == b"\x40\x12\x37\x80":  # .n64, little endian words
        for i in range(0, len(data) - 3, 4):
            data[i:i + 4] = data[i:i + 4][::-1]
    else:
        print("not an N64 ROM", file=sys.stderr)
        return 1
    open(sys.argv[2], "wb").write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
