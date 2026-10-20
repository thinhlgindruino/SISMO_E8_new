"""
ui_size.py - Ước tính dung lượng UartTFT-II_Flash.bin của một project UI_Editor-II (LT7689)
và liệt kê thứ tốn chỗ nhất, file không trang nào dùng, bộ icon bị trùng.

LT7689 không nén ảnh: mỗi pixel 2 byte (RGB565 / softpng / aRGB4444), nên
dung lượng ảnh = rộng x cao x 2. Ảnh nền trang lấy từ "<project>.uiprj", icon lấy từ
các widget trong Plugin/PageXXXX.cfg (cặp firstIcon..lastIcon = cả dải số liên tiếp).

Cách dùng:  python ui_size.py "C:\\SISMO_E8_new\\HMI_E8"  [--limit-mb 128]
"""
import argparse
import collections
import glob
import hashlib
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Can cai Pillow: pip install pillow")

NUM = re.compile(r"(\d{4})_[^,;]*?\.(?:png|jpg|jpeg|bmp)", re.I)


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8-sig", errors="replace")


def image_bytes(path):
    with Image.open(path) as im:
        w, h = im.size
    return w * h * 2, (w, h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("--limit-mb", type=float, default=128)
    ap.add_argument("--top", type=int, default=15)
    a = ap.parse_args()
    root = a.project

    uiprj = glob.glob(os.path.join(root, "*.uiprj"))
    if not uiprj:
        sys.exit("Khong thay file .uiprj trong " + root)

    pictures = {os.path.basename(p): p for p in glob.glob(os.path.join(root, "Picture", "*"))}
    icons = {}
    for p in glob.glob(os.path.join(root, "Icon", "*")):
        if os.path.isfile(p):
            m = re.match(r"(\d{4})", os.path.basename(p))
            if m:
                icons[int(m.group(1))] = p

    used_pics = collections.OrderedDict()
    for line in read_text(uiprj[0]).splitlines():
        if ":" not in line:
            continue
        page, rest = line.split(":", 1)
        name = rest.split(";")[0].strip()
        if name:
            used_pics.setdefault(name, []).append(page.strip())

    used_icons = collections.defaultdict(set)
    for cfg in sorted(glob.glob(os.path.join(root, "Plugin", "Page*.cfg"))):
        page = os.path.basename(cfg)[:8]
        for line in read_text(cfg).splitlines():
            nums = [int(n) for n in NUM.findall(line)]
            widget = line.split(":", 1)[0]
            if len(nums) >= 2 and not widget.startswith(("button", "Page")):
                lo, hi = nums[0], nums[1]
                for n in range(min(lo, hi), max(lo, hi) + 1):
                    used_icons[n].add(f"{page}/{widget}")
            for n in nums:
                used_icons[n].add(f"{page}/{widget}")

    total = 0
    rows = []
    for name, pages in used_pics.items():
        if name in pictures:
            b, size = image_bytes(pictures[name])
            total += b
            rows.append((b, "Picture/" + name, size, ",".join(pages)))
    pic_total = total
    groups = collections.OrderedDict()
    for n in sorted(used_icons):
        if n in icons:
            b, size = image_bytes(icons[n])
            total += b
            key = ",".join(sorted({u.split("/")[0] for u in used_icons[n]}))
            g = groups.setdefault((key, size), [0, 0, n, n])
            g[0] += b; g[1] += 1; g[3] = n
    icon_total = total - pic_total

    mb = lambda b: b / 2 ** 20
    print(f"Anh nen trang : {mb(pic_total):7.1f} MB  ({len(rows)} trang)")
    print(f"Icon dang dung: {mb(icon_total):7.1f} MB")
    print(f"TONG uoc tinh : {mb(total):7.1f} MB  = {total // 1024} KB   (gioi han {a.limit_mb:.0f} MB, con {a.limit_mb - mb(total):.1f} MB)")

    print(f"\n== Nhom icon ton cho nhat ==")
    for (key, size), (b, cnt, lo, hi) in sorted(groups.items(), key=lambda kv: -kv[1][0])[:a.top]:
        print(f"  {mb(b):6.2f} MB  {lo:04d}..{hi:04d} ({cnt} icon {size[0]}x{size[1]})  dung o {key}")

    unused_icons = [n for n in sorted(icons) if n not in used_icons]
    ub = sum(image_bytes(icons[n])[0] for n in unused_icons)
    print(f"\n== Icon khong widget nao dung: {len(unused_icons)} file, {mb(ub):.1f} MB ==")
    for n in unused_icons:
        print("  ", os.path.basename(icons[n]))
    unused_pics = [p for p in pictures if p not in used_pics]
    if unused_pics:
        print("\n== Anh trong Picture khong trang nao dung ==")
        for p in unused_pics:
            print("  ", p)

    print("\n== Icon trung noi dung (co the dung chung) ==")
    seen = collections.defaultdict(list)
    for n in sorted(used_icons):
        if n in icons:
            with open(icons[n], "rb") as f:
                seen[hashlib.md5(f.read()).hexdigest()].append(n)
    dup = [v for v in seen.values() if len(v) > 1]
    for v in dup[:20]:
        print("  ", " = ".join(f"{n:04d}" for n in v))
    if not dup:
        print("   (khong co)")


if __name__ == "__main__":
    main()
