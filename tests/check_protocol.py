#!/usr/bin/env python3
"""check_protocol.py — lab1 自测: 独立核对 QEMU 实跑输出是否符合学号协议。

与课程 check_expect.py 的区别: 本脚本直接读取内核实跑输出(而不是预期文件),
并独立重算 ASCII 校验和, 用于验证"内核输出 == 协议规格"这一事实。

用法:
  python3 tests/check_protocol.py <学号> [实跑输出文件] [协议号]
  # 省略文件时默认读取 tests/kernel_output.txt
  # 省略协议号时按 学号%3 推断; 交叉验证时请显式给出编译期协议号
"""
import sys

SID = int(sys.argv[1]) if len(sys.argv) > 1 else None
PATH = sys.argv[2] if len(sys.argv) > 2 else "tests/kernel_output.txt"
PROTO = int(sys.argv[3]) if len(sys.argv) > 3 else (SID % 3 if SID is not None else None)


def main():
    if SID is None:
        sys.exit(__doc__)

    with open(PATH, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8", "replace")

    proto = PROTO
    want_line = "OSLAB1 sid={} mod97=0x{:x}\n".format(SID, SID % 97)
    errs = []

    if proto == 2:
        lines = text.split("\n")
        if lines[0] + "\n" != want_line:
            errs.append("首行应为 {!r}, 实为 {!r}".format(want_line, lines[0] + "\n"))
        chk = sum(b for b in want_line.encode())
        want_chk = "[chk={}]\n".format(chk)
        if lines[1] + "\n" != want_chk:
            errs.append("第二行应为 {!r}, 实为 {!r}".format(want_chk, lines[1] + "\n"))
        if text != want_line + want_chk:
            errs.append("除这两行外不应有其他输出(实跑字节数 {})".format(len(raw)))
    elif proto == 1:
        want = "".join(ch + "." for ch in want_line)
        if text != want:
            errs.append("协议 1 期望 {!r}".format(want))
    else:
        if text != want_line:
            errs.append("协议 0 期望 {!r}".format(want_line))

    print("学号 {} 协议 {} 实跑输出 {} 字节".format(SID, proto, len(raw)))
    if errs:
        for e in errs:
            print("! " + e)
        print("[FAIL] 内核实跑输出与协议规格不一致")
        sys.exit(1)
    print("[ok] 内核实跑输出与协议规格一致(校验和已独立重算)")


if __name__ == "__main__":
    main()
