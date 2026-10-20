"""
grilles.py - Trang bài tập con 0013 ghép từ ô nhỏ thay vì 10 ảnh lưới 600x921

Cắt 10 lưới gốc E8 (E8_Img, P2_x_x_x_2.jpg) thành ô 190x180 (3 cột x 5 hàng), gộp các ô giống nhau
(thân hình giống nhau và chữ thời gian giống nhau, cho phép lệch 3 px), rồi:
  - ghi Icon 1000.. "case_NN" (mỗi ô khác nhau 1 ảnh) và 1100 / 1101 mũi tên > / <,
  - xóa ảnh lưới cũ 0710..0719,
  - ghi Plugin/Page0013.cfg (15 Icon ô 0x1080..0x108E + Icon mũi tên 0x108F),
  - ghi components/pages/page_program/grid_data.c (bảng ô của 10 lưới).
Chạy:  python grilles.py <thư mục E8_Img> <thư mục HMI_E8> <thư mục e8_rtos>
"""
import os, sys
import numpy as np
from PIL import Image, ImageFilter

GRIDS = ["P2_1_2_1_2", "P2_1_1_1_2", "P2_2_5_1_2_1", "P2_1_5_1_2", "P2_2_5_1_2",
         "P2_1_3_1_2", "P2_1_4_1_2", "P2_2_2_1_2", "P2_2_3_1_2", "P2_3_1_1_2"]
ARROW = {4: 1, 2: 2}
X = [10, 206, 401]
Y = [0, 186, 371, 556, 741]
W, H = 190, 180
TOP = 80
ICON_TILE, ICON_ARROW = 1000, 1100
ADDR_SLOT, ADDR_ARROW = 0x1080, 0x108F
ARROW_POS = (510, 930, 60, 60)
BODY_MAX, LABEL_MAX = 16, 2.5

def old(src, name):
    return Image.open(os.path.join(src, name)).convert("RGB").transpose(Image.Transpose.TRANSPOSE)

def cut(src):
    tiles, layout = [], []
    for name in GRIDS:
        a = np.asarray(old(src, name + ".jpg")).astype(int)
        slots = []
        for y in Y:
            for x in X:
                t = a[y:y + H, x:x + W] if y + H <= a.shape[0] else None
                if t is None or (t.mean(axis=2) > 60).mean() < 0.05:
                    slots.append(None)
                    continue
                slots.append(len(tiles))
                tiles.append(t)
        layout.append(slots)
    return tiles, layout

def blurred(t):
    return np.asarray(Image.fromarray(t.astype("uint8")).filter(ImageFilter.GaussianBlur(1.2))).astype(int)

def shifted(a, b, box, dxs):
    y0, y1, x0, x1 = box
    ref = a[y0:y1, x0:x1].mean(axis=2)
    best = 1e9
    for dy in range(-3, 4):
        for dx in dxs:
            if y0 + dy < 0 or x0 + dx < 0 or x1 + dx > W:
                continue
            best = min(best, np.abs(ref - b[y0 + dy:y1 + dy, x0 + dx:x1 + dx].mean(axis=2)).mean())
    return best

def same(a, b, ba, bb):
    d = np.abs(ba - bb).max(axis=2)
    d[0:40, :] = 0
    if np.percentile(d, 99.9) >= BODY_MAX:
        return False
    if shifted(a, b, (2, 38, 95, 188), range(-3, 3)) >= LABEL_MAX:
        return False
    return shifted(a, b, (2, 38, 2, 80), range(0, 4)) < LABEL_MAX

def group(tiles):
    bl = [blurred(t) for t in tiles]
    rep, uniq = [], []
    for i, t in enumerate(tiles):
        for k, j in enumerate(uniq):
            if same(tiles[j], t, bl[j], bl[i]):
                rep.append(k)
                break
        else:
            rep.append(len(uniq))
            uniq.append(i)
    return rep, uniq

def write_icons(src, hmi, tiles, uniq):
    icon = os.path.join(hmi, "Icon")
    for f in os.listdir(icon):
        n = f[:4]
        if n.isdigit() and (710 <= int(n) <= 719 or ICON_TILE <= int(n) < ICON_TILE + 100 or int(n) in (ICON_ARROW, ICON_ARROW + 1)):
            os.remove(os.path.join(icon, f))
    for k, i in enumerate(uniq):
        Image.fromarray(tiles[i].astype("uint8")).save(os.path.join(icon, "%04d_case_%02d.jpg" % (ICON_TILE + k, k + 1)), quality=95, subsampling=0)
    arrow_l = old(src, "v_13.jpg").crop((40, 20, 80, 60)).resize((60, 60))
    arrow_l.transpose(Image.Transpose.FLIP_LEFT_RIGHT).save(os.path.join(icon, "%04d_fleche_droite.png" % ICON_ARROW))
    arrow_l.save(os.path.join(icon, "%04d_fleche_gauche.png" % (ICON_ARROW + 1)))

def write_cfg(hmi, count):
    path = os.path.join(hmi, "Plugin", "Page0013.cfg")
    with open(path, encoding="utf-8-sig") as f:
        lines = [l for l in f.read().splitlines() if l and not l.startswith("icon_")]
    first, last = "%04d_case_%02d.jpg" % (ICON_TILE, 1), "%04d_case_%02d.jpg" % (ICON_TILE + count - 1, count)
    k = 0
    for r, y in enumerate(Y):
        for c, x in enumerate(X):
            lines.append("icon_%d:0xFFFF,2,0x%04X,2,%d,%d,%d,%d,%s,%s,,,1,%d,icon_%d,Disable;" % (
                k, ADDR_SLOT + k, x, TOP + y, W, H, first, last, count, k))
            k += 1
    ax, ay, aw, ah = ARROW_POS
    lines.append("icon_%d:0xFFFF,2,0x%04X,2,%d,%d,%d,%d,%04d_fleche_droite.png,%04d_fleche_gauche.png,,,1,2,icon_%d,Disable;" % (
        k, ADDR_ARROW, ax, ay, aw, ah, ICON_ARROW, ICON_ARROW + 1, k))
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        f.write("\n".join(lines) + "\n")

def write_c(rtos, layout, rep):
    path = os.path.join(rtos, "components", "pages", "page_program", "grid_data.c")
    L = ["/*",
         " * grid_data.c - Ô của 10 lưới bài tập con trang 0013 (SINH TỰ ĐỘNG bởi tools/grilles.py, không sửa tay)",
         " *",
         " * GRID_TILES[lưới - 1][ô]: số ảnh ô 1..N (icon 1000 + số - 1), 0 = ô trống; ô xếp 3 cột x 5 hàng từ",
         " * trái sang phải, trên xuống dưới. GRID_ARROW: 0 không mũi tên, 1 mũi tên > (sang trang 2), 2 mũi tên <.",
         " */",
         '#include "grid_data.h"',
         "",
         "const uint8_t GRID_TILES[GRID_COUNT][GRID_SLOTS] = {"]
    for slots in layout:
        L.append("    { %s }," % ", ".join(str(rep[s] + 1) if s is not None else "0" for s in slots))
    L.append("};")
    L.append("")
    L.append("const uint8_t GRID_ARROW[GRID_COUNT] = { %s };" % ", ".join(str(ARROW.get(g, 0)) for g in range(len(GRIDS))))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")

if __name__ == "__main__":
    src, hmi, rtos = sys.argv[1:4]
    tiles, layout = cut(src)
    rep, uniq = group(tiles)
    write_icons(src, hmi, tiles, uniq)
    write_cfg(hmi, len(uniq))
    write_c(rtos, layout, rep)
    print("%d ô, %d ảnh khác nhau (%.2f MB thay cho 10.55 MB)" % (len(tiles), len(uniq), len(uniq) * W * H * 2 / 1048576))
