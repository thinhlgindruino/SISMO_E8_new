"""
langues.py - Chữ 6 ngôn ngữ (Français, English, Español, Italiano, Deutsch, Português) cho UI HMI_E8

Lấy chữ từ bộ ảnh gốc E8 (E8_Img: name1..name5 + y_name6 = 98 đoạn chữ, name_lg_* tên ngôn ngữ,
z_nameN_* thông báo / nút gạt), rồi:
  - sinh lại ảnh nền KHÔNG chữ (chữ cũ được thay bằng phần nền gốc tương ứng),
  - mỗi chỗ có chữ = 1 Icon, 6 ảnh liền nhau theo thứ tự ngôn ngữ, ảnh = phần nền ở đúng chỗ đó +
    chữ đè lên (giữ khử răng cưa),
  - chữ cố định đọc biến ngôn ngữ 0x1030 (0..5); chữ phụ thuộc trạng thái (tên bài, Statut, Accès,
    Mode, Timer, Configuration WIFI) dùng địa chỉ cũ, ESP32 ghi trạng thái * 6 + ngôn ngữ + 1,
  - sửa Plugin/PageXXXX.cfg và "Sismo Fitness.uiprj".
Chạy 1 lần trên bản UI chưa có đa ngôn ngữ (bản 1.10.0):
    python langues.py <thư mục E8_Img> <thư mục HMI_E8>
"""
import os, sys, re
import numpy as np
from PIL import Image

LANGS = ["fr", "en", "es", "it", "de", "pt"]
PRE = ["name1", "name2", "name3", "name4", "name5", "y_name6"]
DARK = (14, 17, 26)
BAND = (0, 18, 30)
ADDR_LANG, ADDR_FIXED = 0x1030, 0x105E
ADDR_TITLE, ADDR_TITLE_SHORT = 0x1051, 0x105B
ADDR_NET, ADDR_MODE, ADDR_TIMER, ADDR_ACCESS, ADDR_STATUS = 0x1040, 0x1042, 0x1043, 0x1046, 0x1047
ADDR_PROG_BUTTON = 0x1059
LONG_X, LONG_W, SHORT_W, TITLE_Y = 55, 526, 190, 520
COLS = (10, 210, 410)
ROWS = (450, 910)
MUSCLES = {
    "0004": [(81, 0, 0), (82, 1, 0), (83, 2, 0), (84, 0, 1), (85, 1, 1)],
    "0021": [(82, 0, 0), (83, 1, 0), (86, 2, 0), (87, 0, 1), (85, 1, 1)],
    "0022": [(84, 1, 0)],
    "0023": [(88, 1, 0), (89, 1, 1)],
    "0024": [(90, 0, 0), (91, 1, 0), (92, 2, 0)],
}
MUSCLE_BASE = {"0004": "P2_1", "0021": "P2_2", "0022": "P2_3", "0023": "P2_4", "0024": "P2_5"}
MENU = [(73, 10, 400), (74, 310, 400), (75, 10, 780), (76, 310, 780)]
HEADERS = {"0002": (73, 86), "0011": (73, 86), "0010": (76, 86), "0003": (74, 110), "0007": (75, 100)}
TILES = [(93, 35, 200), (94, 318, 200), (95, 35, 400), (96, 318, 400), (97, 35, 600), (98, 318, 600)]
TILE_SCALE = 1.2
LEVEL_X, LEVEL_Y = (37, 225, 413), 150

SRC = HMI = None
icons = {}

def old(name):
    return Image.open(os.path.join(SRC, name)).convert("RGB").transpose(Image.Transpose.TRANSPOSE)

def text(L, k):
    return old("%s_%d.jpg" % (PRE[L], k))

def lang_name(L):
    return old("name_lg_%d.jpg" % (L + 1) if L < 5 else "y_name_lg_6.jpg")

def ztext(L, key):
    return old("z_name%d_%s.jpg" % (L + 1, key))

def pic(name):
    return Image.open(os.path.join(HMI, "Picture", name)).convert("RGB")

def save_pic(img, name):
    img.save(os.path.join(HMI, "Picture", name), quality=92, subsampling=0)

def alpha(t):
    a = np.asarray(t.convert("L")).astype(float)
    edge = np.concatenate([a[0], a[-1], a[:, 0], a[:, -1]])
    bg = np.median(edge)
    return np.clip((a - bg) / max(1.0, 255 - bg), 0, 1)

def over(base, t):
    b = np.asarray(base).astype(float)
    a = alpha(t)[..., None]
    out = b * (1 - a) + 255 * a
    return Image.fromarray(out.round().clip(0, 255).astype("uint8"))

def plain(w, h, color=DARK):
    return Image.new("RGB", (w, h), color)

def centered(t, w, h, color=DARK):
    canvas = plain(w, h, color)
    tw, th = t.size
    patch = canvas.crop(((w - tw) // 2, (h - th) // 2, (w - tw) // 2 + tw, (h - th) // 2 + th))
    canvas.paste(over(patch, t), ((w - tw) // 2, (h - th) // 2))
    return canvas

def add_icons(num, name, images, fmt="jpg"):
    files = []
    for i, im in enumerate(images):
        suffix = LANGS[i % 6] if len(images) % 6 == 0 else "%d" % i
        files.append("%04d_%s_%s.%s" % (num + i, name, suffix, fmt))
        path = os.path.join(HMI, "Icon", files[-1])
        if fmt == "jpg":
            im.save(path, quality=95, subsampling=0)
        else:
            im.save(path)
    icons[num] = files
    return files

def icon_line(idx, addr, x, y, w, h, files, lo, hi):
    return "icon_%d:0xFFFF,2,0x%04X,2,%d,%d,%d,%d,%s,%s,,,%d,%d,icon_%d,Disable;" % (idx, addr, x, y, w, h, files[0], files[-1], lo, hi, idx)

def read_cfg(page):
    path = os.path.join(HMI, "Plugin", "Page%s.cfg" % page)
    with open(path, encoding="utf-8-sig") as f:
        return [l for l in f.read().splitlines() if l]

def write_cfg(page, lines):
    path = os.path.join(HMI, "Plugin", "Page%s.cfg" % page)
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        f.write("\n".join(lines) + "\n")

def next_icon(lines):
    used = [int(m.group(1)) for l in lines for m in [re.match(r"icon_(\d+):", l)] if m]
    return max(used) + 1 if used else 0

def append_icons(page, specs):
    lines = read_cfg(page)
    k = next_icon(lines)
    for addr, x, y, w, h, files, lo, hi in specs:
        lines.append(icon_line(k, addr, x, y, w, h, files, lo, hi))
        k += 1
    write_cfg(page, lines)

def set_page_picture(page, picture):
    path = os.path.join(HMI, "Sismo Fitness.uiprj")
    with open(path, encoding="utf-8-sig") as f:
        lines = f.read().split("\n")
    name = "Page%s" % page
    for i, l in enumerate(lines):
        if l.startswith("Page%s:" % page):
            name = os.path.splitext(l.split(":", 1)[1].split(";")[1])[0]
            lines[i] = "Page%s:%s;%s;RGBColor(0,0,0);" % (page, picture, name)
    with open(path, "w", encoding="utf-8-sig", newline="") as f:
        f.write("\n".join(lines))
    cfg = read_cfg(page)
    header = "Page:,0x0000,,0x0000,,0x0000,,0x0000,%s;RGBColor(0,0,0);%s;Disable;0;0;Disable;" % (picture, name)
    if cfg and cfg[0].startswith("Page:"):
        cfg[0] = header
    else:
        cfg.insert(0, header)
    write_cfg(page, cfg)

def patch(img, base, box):
    x, y, w, h = box
    img.paste(base.crop((x, y, x + w, y + h)), (x, y))

def titles():
    long_imgs, short_imgs = [], []
    for k in range(1, 49):
        for L in range(6):
            long_imgs.append(text(L, k).crop((LONG_X, 0, LONG_X + LONG_W, 40)))
    for k in range(49, 61):
        for L in range(6):
            t = text(L, k)
            long_t = over(plain(t.width, t.height, BAND), t)
            short_imgs.append(long_t.crop((0, 0, SHORT_W, 40)))
    fl = add_icons(1200, "titre", long_imgs)
    fs = add_icons(1500, "titre_court", short_imgs)
    for page in ("0006", "0014"):
        lines = read_cfg(page)
        out = []
        for l in lines:
            if l.startswith("icon_") and ",0x%04X," % ADDR_TITLE in l:
                idx = re.match(r"icon_(\d+):", l).group(1)
                out.append(icon_line(int(idx), ADDR_TITLE, LONG_X, TITLE_Y, LONG_W, 40, fl, 1, len(fl)))
                out.append(icon_line(next_icon(lines) + 0, ADDR_TITLE_SHORT, 0, TITLE_Y, SHORT_W, 40, fs, 1, len(fs)))
            else:
                out.append(l)
        write_cfg(page, out)
    for f in os.listdir(os.path.join(HMI, "Icon")):
        if f[:4].isdigit() and 540 <= int(f[:4]) <= 599:
            os.remove(os.path.join(HMI, "Icon", f))

def menu_and_headers():
    p1 = old("P1.jpg")
    menu = pic("0001_Menu_principal.jpg")
    for k, x, y in MENU:
        patch(menu, p1, (x, y, 280, 40))
    save_pic(menu, "0001_Menu_principal.jpg")
    specs = []
    for n, (k, x, y) in enumerate(MENU):
        files = add_icons(1600 + 6 * n, "menu_%d" % k, [over(menu.crop((x, y, x + 280, y + 40)), text(L, k)) for L in range(6)])
        specs.append((ADDR_LANG, x, y, 280, 40, files, 0, 5))
    append_icons("0001", specs)

    duree = pic("0002_Duree_01min.jpg")
    patch(duree, p1, (130, 80, 340, 60))
    save_pic(duree, "0002_Duree_01min.jpg")
    seance = pic("0010_Seance_Manuel.jpg")
    patch(seance, p1, (160, 86, 280, 40))
    save_pic(seance, "0010_Seance_Manuel.jpg")
    perso = pic("0007_Perso.jpg")
    patch(perso, old("P2_6.jpg"), (160, 100, 280, 40))
    save_pic(perso, "0007_Perso.jpg")
    bases = {"0002": duree, "0011": seance, "0010": seance, "0003": p1, "0007": perso}
    sets = {}
    for page, (k, y) in HEADERS.items():
        key = (k, y)
        if key not in sets:
            base = bases[page].crop((160, y, 440, y + 40))
            sets[key] = add_icons(1630 + 6 * len(sets), "titre_page_%d" % k, [over(base, text(L, k)) for L in range(6)])
        append_icons(page, [(ADDR_LANG, 160, y, 280, 40, sets[key], 0, 5)])
    set_page_picture("0011", "0010_Seance_Manuel.jpg")
    set_page_picture("0003", "0027_Parametres.jpg")
    for name in ("0011_Seance_Duree.jpg", "0003_Programmes.jpg"):
        p = os.path.join(HMI, "Picture", name)
        if os.path.exists(p):
            os.remove(p)

def tiles():
    specs = []
    for n, (k, x, y) in enumerate(TILES):
        imgs = []
        for L in range(6):
            t = text(L, k)
            imgs.append(t.resize((round(t.width * TILE_SCALE), round(t.height * TILE_SCALE)), Image.LANCZOS))
        files = add_icons(1660 + 6 * n, "objectif_%d" % k, imgs)
        specs.append((ADDR_LANG, x, y, imgs[0].width, imgs[0].height, files, 0, 5))
    append_icons("0003", specs)

def muscles():
    sets = []
    num = 1700
    for page, items in MUSCLES.items():
        name = [f for f in os.listdir(os.path.join(HMI, "Picture")) if f.startswith(page)][0]
        img = pic(name)
        base = old(MUSCLE_BASE[page] + ".jpg")
        for k, c, r in items:
            patch(img, base, (COLS[c], ROWS[r], 180, 80))
        save_pic(img, name)
        specs = []
        for k, c, r in items:
            x, y = COLS[c], ROWS[r]
            imgs = [over(img.crop((x, y, x + 180, y + 80)), text(L, k)) for L in range(6)]
            arr = np.stack([np.asarray(i).astype(int) for i in imgs])
            for sk, sarr, files in sets:
                if sk == k and np.abs(sarr - arr).max() <= 6:
                    break
            else:
                files = add_icons(num, "muscle_%d" % k, imgs)
                num += 6
                sets.append((k, arr, files))
            specs.append((ADDR_LANG, x, y, 180, 80, files, 0, 5))
        append_icons(page, specs)

def level_page():
    lvl = old("P1.jpg")
    for i, k in enumerate((1, 2, 3)):
        lvl.paste(old("P2_1_5_%d.jpg" % k), (LEVEL_X[i], LEVEL_Y))
    save_pic(lvl, "0005_Niveau.jpg")
    specs = []
    for i, k in enumerate((77, 78, 79)):
        x, y = LEVEL_X[i] + 15, LEVEL_Y + 62
        files = add_icons(1800 + 6 * i, "niveau_%d" % k, [over(lvl.crop((x, y, x + 120, y + 40)), text(L, k)) for L in range(6)])
        specs.append((ADDR_LANG, x, y, 120, 40, files, 0, 5))
    files = add_icons(1820, "niveau_titre", [over(lvl.crop((160, 820, 440, 860)), text(L, 61)) for L in range(6)])
    specs.append((ADDR_LANG, 160, 820, 280, 40, files, 0, 5))
    append_icons("0005", specs)
    lines = read_cfg("0005")
    lines = [("varAdj_9:0,0,130,80,0x%04X,1,6,6,ushort,+,Stop,Once,,,varAdj_9,Enable,Disable,0x0000;" % ADDR_PROG_BUTTON)
             if l.startswith("button_0:") else l for l in lines]
    write_cfg("0005", lines)

def frames():
    f70 = Image.open(os.path.join(HMI, "Icon", "0412_mode_1.jpg")).convert("RGB")
    f70.paste(plain(380, 50), (20, 10))
    f80 = Image.open(os.path.join(HMI, "Icon", "0437_acces_libre.jpg")).convert("RGB")
    f80.paste(plain(380, 56), (20, 12))
    return add_icons(1830, "cadre", [f70]), add_icons(1831, "cadre", [f80])

def replace_icon(page, addr, new_specs):
    lines = read_cfg(page)
    out, k = [], next_icon(lines)
    for l in lines:
        if l.startswith("icon_") and ",0x%04X," % addr in l:
            for a, x, y, w, h, files, lo, hi in new_specs:
                out.append(icon_line(k, a, x, y, w, h, files, lo, hi))
                k += 1
        else:
            out.append(l)
    write_cfg(page, out)

def settings():
    f70, f80 = frames()
    icon = os.path.join(HMI, "Icon")
    def inner(name):
        return Image.open(os.path.join(icon, name)).convert("RGB").crop((20, 10, 400, 60))
    net = [inner("0400_nut_offline.jpg"), inner("0402_nut_online.jpg")] + [centered(ztext(L, "setup_wifi"), 380, 50) for L in range(6)]
    fn = add_icons(1940, "wifi", net)
    replace_icon("0027", ADDR_NET, [(ADDR_FIXED, 90, 405, 420, 70, f70, 0, 0), (ADDR_NET, 110, 415, 380, 50, fn, 1, 8)])

    status = []
    for k in (63, 62):
        for L in range(6):
            status.append(over(plain(500, 40), text(L, k).crop((50, 0, 550, 40))))
    fs = add_icons(1840, "statut", status)
    access = [ztext(L, key) for key in ("card1", "card2") for L in range(6)]
    fa = add_icons(1860, "acces", access)
    fl = add_icons(1880, "langue", [centered(lang_name(L), 380, 50) for L in range(6)])
    replace_icon("0029", ADDR_STATUS, [(ADDR_STATUS, 50, 110, 500, 40, fs, 1, 12)])
    replace_icon("0029", ADDR_ACCESS, [(ADDR_FIXED, 90, 400, 420, 80, f80, 0, 0), (ADDR_ACCESS, 110, 415, 380, 50, fa, 1, 12)])
    append_icons("0029", [(ADDR_FIXED, 90, 555, 420, 70, f70, 0, 0), (ADDR_LANG, 110, 565, 380, 50, fl, 0, 5)])
    lines = read_cfg("0029")
    lines.append("varAdj_9:90,555,420,70,0x%04X,1,0,5,ushort,+,Loop,Once,,,varAdj_9,Enable,Disable,0x0000;" % ADDR_LANG)
    write_cfg("0029", lines)

    modes = [centered(text(L, 68 + m), 380, 50) for m in range(5) for L in range(6)]
    fm = add_icons(1890, "mode", modes)
    timer = [ztext(L, key) for key in ("timer1", "timer2") for L in range(6)]
    ft = add_icons(1920, "timer", timer)
    replace_icon("0030", ADDR_MODE, [(ADDR_FIXED, 90, 300, 420, 70, f70, 0, 0), (ADDR_MODE, 110, 310, 380, 50, fm, 1, 30)])
    replace_icon("0030", ADDR_TIMER, [(ADDR_FIXED, 90, 420, 420, 70, f70, 0, 0), (ADDR_TIMER, 110, 430, 380, 50, ft, 1, 12)])
    for f in os.listdir(icon):
        n = f[:4]
        if n.isdigit() and (400 <= int(n) <= 402 or 412 <= int(n) <= 418 or 437 <= int(n) <= 440):
            os.remove(os.path.join(icon, f))

def back_buttons():
    lines = read_cfg("0025")
    lines = [l.replace(",Page0001,", ",Page0003,") if l.startswith("button_6:") else l for l in lines]
    write_cfg("0025", lines)

if __name__ == "__main__":
    SRC, HMI = sys.argv[1], sys.argv[2]
    titles()
    menu_and_headers()
    tiles()
    muscles()
    level_page()
    settings()
    back_buttons()
    print("Đã tạo %d icon chữ" % sum(len(v) for v in icons.values()))
