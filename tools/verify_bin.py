#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 DWS 产出的月度 .bin/.idx 是否符合 FR 的读取约定。

用法：
    python tools/verify_bin.py /tmp/dws_out/202501.bin
    python tools/verify_bin.py /tmp/dws_out          # 校验目录下所有 .bin

FR 月读路径（GX_QuotaMgr.cpp）：
    magic == "FBM1", rec_size == 96, 数据从 20 + off*96 开始。
"""
import glob
import os
import struct
import sys

REC_SIZE = 96
HEADER_SIZE = 20


def verify_one(bin_path: str) -> bool:
    ok = True
    idx_path = bin_path[:-4] + ".idx" if bin_path.endswith(".bin") else bin_path + ".idx"
    with open(bin_path, "rb") as f:
        head = f.read(HEADER_SIZE)
        if len(head) < HEADER_SIZE:
            print(f"FAIL {bin_path}: 文件不足 {HEADER_SIZE} 字节")
            return False
        magic, rec, count, reserved = struct.unpack("<4sIQI", head)
        size = os.path.getsize(bin_path)
        if magic != b"FBM1":
            print(f"FAIL {bin_path}: magic={magic!r}，应为 b'FBM1'")
            ok = False
        if rec != REC_SIZE:
            print(f"FAIL {bin_path}: rec_size={rec}，应为 {REC_SIZE}")
            ok = False
        if size != HEADER_SIZE + count * REC_SIZE:
            print(f"FAIL {bin_path}: size={size}，count={count}，"
                  f"应为 {HEADER_SIZE + count * REC_SIZE}")
            ok = False

    if not os.path.exists(idx_path):
        print(f"FAIL {idx_path}: 索引不存在")
        return False
    with open(idx_path, "rb") as f:
        raw = f.read()
    if len(raw) < 4:
        print(f"FAIL {idx_path}: 索引不足 4 字节")
        return False
    n = struct.unpack_from("<i", raw, 0)[0]
    if len(raw) != 4 + n * 20:
        print(f"FAIL {idx_path}: 大小={len(raw)}，n={n}，应为 {4 + n * 20}")
        ok = False

    total = 0
    for k in range(n):
        day, off, cnt = struct.unpack_from("<iqq", raw, 4 + k * 20)
        if off != total:
            print(f"FAIL {idx_path}: 第 {k} 天 {day} offset={off}，应为 {total}")
            ok = False
        total += cnt
    if total != count:
        print(f"FAIL {idx_path}: 索引计数合计={total}，头 count={count}")
        ok = False

    print(f"{'PASS' if ok else 'FAIL'} {bin_path}  rec={rec} count={count} "
          f"days={n} size={size}")
    return ok


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    target = sys.argv[1]
    files = sorted(glob.glob(os.path.join(target, "*.bin"))) if os.path.isdir(target) else [target]
    if not files:
        print("没有找到 .bin")
        return 2
    return 0 if all(verify_one(p) for p in files) else 1


if __name__ == "__main__":
    sys.exit(main())
