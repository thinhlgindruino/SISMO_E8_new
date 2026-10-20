"""
messages.py - Thông báo lỗi 6 ngôn ngữ (ERR.01 / 02 / 03 / 04, "Veuillez badger svp") cho UI HMI_E8

Ảnh lấy từ bộ ảnh gốc E8: z_nameN_err1 / err2 / err4 (380x70, theo ngôn ngữ), name_err_03 (chung mọi ngôn
ngữ), name1..name5 / y_name6 số 80 ("Veuillez badger svp"). Ảnh 360x40 được đặt giữa nền xám 380x70.
Icon 1960..1989 = thông báo t (0 ERR.01, 1 ERR.02, 2 ERR.03, 3 ERR.04, 4 badge) * 6 + ngôn ngữ, cùng 1 Icon
0x1031 (giá trị LANG_ICON(t, ngôn ngữ), 0 = ẩn) trên trang khởi động 0000 và các trang buổi tập 0006 /
0010 / 0011 / 0014 (dải trống dưới hàng nút). Trang 0000: nút chạm toàn màn hình (trước là nút chuyển thẳng
sang 0001) đổi thành Variable Button 0x1032 = 1 báo về ESP32 (ESP32 chỉ cho vào menu khi CM8 bình thường).
Chạy lại nhiều lần được (xóa icon / widget cũ trước).
    python messages.py <thư mục E8_Img> <thư mục HMI_E8>
"""
import os, sys, re
from PIL import Image

PRE = ["name1", "name2", "name3", "name4", "name5", "y_name6"]
LANGS = ["fr", "en", "es", "it", "de", "pt"]
NAMES = ["err01", "err02", "err03", "err04", "badge"]
ICON_FIRST = 1960
ADDR_MESSAGE = 0x1031
ADDR_STARTUP_TOUCH = 0x1032
W, H = 380, 70
GRAY = (217, 217, 217)
PAGES = {"0000": (110, 760), "0006": (110, 925), "0010": (110, 925), "0011": (110, 925), "0014": (110, 925)}

def old(src, name):
    return Image.open(os.path.join(src, name)).convert("RGB").transpose(Image.Transpose.TRANSPOSE)

def boxed(img):
    canvas = Image.new("RGB", (W, H), GRAY)
    canvas.paste(img, ((W - img.width) // 2, (H - img.height) // 2))
    return canvas

def message(src, t, L):
    if t == 0:
        return old(src, "z_name%d_err1.jpg" % (L + 1))
    if t == 1:
        return old(src, "z_name%d_err2.jpg" % (L + 1))
    if t == 2:
        return boxed(old(src, "name_err_03.jpg"))
    if t == 3:
        return old(src, "z_name%d_err4.jpg" % (L + 1))
    return boxed(old(src, "%s_80.jpg" % PRE[L]))

def write_icons(src, hmi):
    icon = os.path.join(hmi, "Icon")
    for f in os.listdir(icon):
        if f[:4].isdigit() and ICON_FIRST <= int(f[:4]) < ICON_FIRST + 30:
            os.remove(os.path.join(icon, f))
    files = []
    for t in range(len(NAMES)):
        for L in range(6):
            files.append("%04d_message_%s_%s.jpg" % (ICON_FIRST + t * 6 + L, NAMES[t], LANGS[L]))
            im = message(src, t, L)
            assert im.size == (W, H), (files[-1], im.size)
            im.save(os.path.join(icon, files[-1]), quality=95, subsampling=0)
    return files

def write_cfg(hmi, files):
    for page, (x, y) in PAGES.items():
        path = os.path.join(hmi, "Plugin", "Page%s.cfg" % page)
        with open(path, encoding="utf-8-sig") as f:
            lines = [l for l in f.read().splitlines() if l and ",0x%04X," % ADDR_MESSAGE not in l]
        used = [int(m.group(1)) for l in lines for m in [re.match(r"icon_(\d+):", l)] if m]
        k = max(used) + 1 if used else 0
        lines.append("icon_%d:0xFFFF,2,0x%04X,2,%d,%d,%d,%d,%s,%s,,,1,%d,icon_%d,Disable;" % (
            k, ADDR_MESSAGE, x, y, W, H, files[0], files[-1], len(files), k))
        with open(path, "w", encoding="utf-8-sig", newline="") as f:
            f.write("\n".join(lines) + "\n")

def startup_touch(hmi):
    path = os.path.join(hmi, "Plugin", "Page0000.cfg")
    with open(path, encoding="utf-8-sig") as f:
        lines = [l for l in f.read().splitlines() if l]
    out = []
    for l in lines:
        if l.startswith("button_0:0,0,600,1024,"):
            l = "varAdj_9:0,0,600,1024,0x%04X,1,1,1,ushort,+,Stop,Once,,,varAdj_9,Enable,Disable,0x0000;" % ADDR_STARTUP_TOUCH
        out.append(l)
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        f.write("\n".join(out) + "\n")

if __name__ == "__main__":
    src, hmi = sys.argv[1], sys.argv[2]
    files = write_icons(src, hmi)
    write_cfg(hmi, files)
    startup_touch(hmi)
    print("%d ảnh thông báo, Icon 0x%04X trên trang %s" % (len(files), ADDR_MESSAGE, ", ".join(PAGES)))
