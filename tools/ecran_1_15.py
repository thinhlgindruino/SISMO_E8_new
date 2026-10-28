"""
ecran_1_15.py - Sửa giao diện HMI_E8 cho firmware 1.15.0 (góp ý 28/10)

  1. Trang 74766 (0029): dòng "E8 CM8 - Statut : ..." canh giữa màn hình (12 ảnh 1840..1851, 2 trạng thái x 6 ngôn ngữ).
  2. Trang 74710 (0028): nút chế độ có 3 giá trị 0 MODE WIFI / 1 MODE LOCAL / 2 MODE LAN (ảnh 0410, 0411, 0412 mới),
     mỗi ảnh = khung bo tròn cũ + đúng 1 chữ canh giữa; chữ "LAN" ghép từ chữ L, A của "LOCAL" và chữ N của "ONLINE"
     (cùng font máy E8 cũ).
  3. Bỏ tiêu đề trang (Durée Exercices, Programmes, Perso, Manuel): xóa Icon 0x1030 trên 0002 / 0003 / 0007 / 0010 /
     0011 và ảnh 1630..1653 (ảnh nền các trang này đã không có chữ).
  4. Trang 0011: vòng cường độ y 138 -> 140 như trang 0010 (lệch 2 px làm lộ viền xanh của ảnh nền ở khung đồng hồ).
  5. Thông báo lỗi: dải bo tròn 2 đầu (bán nguyệt, như máy E8 cũ) 460x70 ở PILL_X, PILL_Y (cạnh dưới 1/4 màn hình),
     màu FILL, chữ lấy từ ảnh thông báo cũ 380x70 (tách chữ khỏi nền rồi đặt lên giữa dải).
     - trang 0000: 1 Icon 0x1031 460x70, ảnh 1960..1989 = dải đã ghép sẵn lên nền trang 0000 ở đúng chỗ đó;
     - trang buổi tập 0006 / 0010 / 0011 / 0014: dải nằm đè lên vòng cường độ (0x1000 = 0..31) / hình minh họa
       (0x1050 = 1..32). Màn hình chỉ vẽ lại widget vừa đổi giá trị và Icon ẩn thì vẽ ảnh nền vào chỗ của nó, nên dải
       chia 3 Icon, mỗi Icon có thêm 32 "mảnh" = đúng phần vòng cường độ / hình minh họa k ở chỗ đó; ESP32 không bao giờ
       ghi 0 (ẩn) mà ghi mảnh đang nằm bên dưới. Cả 3 Icon đặt TRƯỚC Icon bên dưới trong danh sách widget (lúc vào
       trang / Icon bên dưới đổi thì nó vẽ đè lên, không bao giờ lộ nền).
         giữa  0x1031  x 105, 390x70: 1..30 thông báo (phần thẳng của dải), 31..62 mảnh k = 0..31
         2 đầu 0x1033  x 70 / x 495, 35x70: 1..32 mảnh k, 33..64 đầu bán nguyệt ghép lên mảnh k
       Ảnh: vòng cường độ 2100..2161 (giữa), 2300..2363 / 2400..2463 (đầu trái / phải);
            hình minh họa 2200..2261, 2500..2563 / 2600..2663.
Chạy 1 lần trên bản UI của firmware 1.14.3:
    python ecran_1_15.py <thư mục HMI_E8>
"""
import os, sys, re, glob, shutil
import numpy as np
from PIL import Image

DARK = (14, 17, 26)
LANGS = ["fr", "en", "es", "it", "de", "pt"]
ADDR_LANG, ADDR_MESSAGE, ADDR_MODE_ICON, ADDR_STATUS = 0x1030, 0x1031, 0x1041, 0x1047
ADDR_INTENSITY, ADDR_ILLUS = 0x1000, 0x1050
ADDR_MESSAGE_CAPS = 0x1033
PILL_X, PILL_Y, PILL_W, PILL_H = 70, 230, 460, 70
CAP_W = 35
MID_X, MID_W = PILL_X + CAP_W, PILL_W - 2 * CAP_W
FILL = (239, 233, 233)
MSG_FIRST, MSG_COUNT = 1960, 30
MSG_NAMES = ["err01", "err02", "err03", "err04", "badge"]
PIECES = 32
TITLE_PAGES = ["0002", "0003", "0007", "0010", "0011"]
TITLE_ICONS = range(1630, 1654)
FAN_PAGES = ["0010", "0011"]
ILLUS_PAGES = ["0006", "0014"]
FAN_Y = 140
SETS = {
    "cuongdo": {"pages": FAN_PAGES,   "addr": ADDR_INTENSITY, "mid": 2100, "cap_g": 2300, "cap_d": 2400},
    "illus":   {"pages": ILLUS_PAGES, "addr": ADDR_ILLUS,     "mid": 2200, "cap_g": 2500, "cap_d": 2600},
}

HMI = None


def icon_path(num):
    found = glob.glob(os.path.join(HMI, "Icon", "%04d_*" % num))
    return found[0] if found else None


def read_cfg(page):
    with open(os.path.join(HMI, "Plugin", "Page%s.cfg" % page), encoding="utf-8-sig") as f:
        return [l for l in f.read().splitlines() if l]


def write_cfg(page, lines):
    with open(os.path.join(HMI, "Plugin", "Page%s.cfg" % page), "w", encoding="utf-8-sig", newline="") as f:
        f.write("\n".join(lines) + "\n")


def icon_fields(line):
    m = re.match(r"(icon_\d+):(.*)", line)
    return (m.group(1), m.group(2).split(",")) if m else (None, None)


def page_background(page):
    for l in read_cfg(page):
        if l.startswith("Page:"):
            return l.split(",")[8].split(";")[0]
    with open(os.path.join(HMI, "Sismo Fitness.uiprj"), encoding="utf-8-sig") as f:
        for l in f:
            if l.startswith("Page%s:" % page):
                return l.split(":", 1)[1].split(";")[0]


def save_jpg(im, path):
    im.convert("RGB").save(path, quality=95, subsampling=0)


def center_status():
    for num in range(1840, 1852):
        path = icon_path(num)
        a = np.array(Image.open(path).convert("RGB")).astype(int)
        diff = np.abs(a - np.array(DARK)).sum(2) > 45
        xs = np.where(diff.any(0))[0]
        x0, x1 = max(xs.min() - 2, 0), min(xs.max() + 3, a.shape[1])
        text = Image.fromarray(a[:, x0:x1].astype(np.uint8))
        out = Image.new("RGB", (a.shape[1], a.shape[0]), DARK)
        out.paste(text, ((a.shape[1] - text.width) // 2, 0))
        save_jpg(out, path)


def glyphs(img, x0, x1, thr=120):
    a = np.array(img.convert("L"))[9:61, x0:x1 + 1] > thr
    runs, start = [], None
    for x, on in enumerate(a.any(0)):
        if on and start is None:
            start = x
        if not on and start is not None:
            runs.append((x0 + start, x0 + x - 1))
            start = None
    if start is not None:
        runs.append((x0 + start, x1))
    return runs


def mode_icons():
    wifi_path, local_path = icon_path(410), icon_path(411)
    wifi = Image.open(wifi_path).convert("RGB")
    local = Image.open(local_path).convert("RGB")
    online = Image.open(icon_path(1941)).convert("RGB")

    frame = wifi.copy()
    frame.paste(DARK, (22, 9, 398, 61))
    frame.paste(DARK, (398, 14, 410, 57))
    frame.paste(DARK, (10, 14, 22, 57))

    wifi_text = wifi.crop((47, 9, 170, 61))
    local_text = local.crop((250, 9, 400, 61))

    runs = glyphs(local, 250, 400)
    mode_part = local.crop((250, 9, runs[3][1] + 1, 61))
    l_glyph = local.crop((runs[4][0], 9, runs[4][1] + 1, 61))
    a_glyph = local.crop((runs[7][0], 9, runs[7][1] + 1, 61))
    space = runs[4][0] - runs[3][1] - 1
    n0, n1 = glyphs_online(online)[1]
    on_rows = np.where((np.array(online.convert("L")) > 120).any(1))[0]
    loc_rows = np.where((np.array(local.convert("L"))[9:61, runs[4][0]:runs[4][1] + 1] > 120).any(1))[0] + 9
    n_glyph = online.crop((n0, 0, n1 + 1, online.height))
    scale = (loc_rows.max() - loc_rows.min() + 1) / (on_rows.max() - on_rows.min() + 1)
    if abs(scale - 1) > 0.04:
        n_glyph = n_glyph.resize((round(n_glyph.width * scale), round(n_glyph.height * scale)), Image.LANCZOS)
        on_top = round(on_rows.min() * scale)
    else:
        on_top = on_rows.min()
    lan_w = mode_part.width + space + l_glyph.width + 3 + a_glyph.width + 4 + n_glyph.width
    lan = Image.new("RGB", (lan_w, 52), DARK)
    x = 0
    for g in (mode_part,):
        lan.paste(g, (x, 0)); x += g.width
    x += space
    lan.paste(l_glyph, (x, 0)); x += l_glyph.width + 3
    lan.paste(a_glyph, (x, 0)); x += a_glyph.width + 4
    ny = (loc_rows.min() - 9) - on_top
    n_box = Image.new("RGB", (n_glyph.width, 52), DARK)
    n_box.paste(n_glyph, (0, ny))
    lan.paste(n_box, (x, 0))

    for path, text in ((wifi_path, wifi_text), (local_path, local_text),
                       (os.path.join(HMI, "Icon", "0412_mode_lan.jpg"), lan)):
        out = frame.copy()
        t = trim(text)
        out.paste(t, ((out.width - t.width) // 2, 9))
        save_jpg(out, path)

    lines = read_cfg("0028")
    out = []
    for l in lines:
        name, p = icon_fields(l)
        if name and int(p[2], 16) == ADDR_MODE_ICON:
            p[9] = "0412_mode_lan.jpg"
            p[13] = "2"
            l = "%s:%s" % (name, ",".join(p))
        out.append(l)
    write_cfg("0028", out)


def glyphs_online(img):
    a = np.array(img.convert("L")) > 120
    runs, start = [], None
    for x, on in enumerate(a.any(0)):
        if on and start is None:
            start = x
        if not on and start is not None:
            runs.append((start, x - 1))
            start = None
    return runs


def trim(text):
    g = np.array(text.convert("L"))
    rows = np.where((g > 120).any(1))[0]
    clean = np.array(text.convert("RGB"))
    clean[:max(rows.min() - 2, 0)] = DARK
    clean[rows.max() + 3:] = DARK
    text = Image.fromarray(clean)
    xs = np.where((np.array(text.convert("L")) > 60).any(0))[0]
    return text.crop((max(xs.min() - 1, 0), 0, min(xs.max() + 2, text.width), text.height))


def remove_titles():
    for page in TITLE_PAGES:
        lines = []
        for l in read_cfg(page):
            name, p = icon_fields(l)
            if name and int(p[2], 16) == ADDR_LANG and "titre_page" in p[8]:
                continue
            lines.append(l)
        write_cfg(page, lines)
    for num in TITLE_ICONS:
        p = icon_path(num)
        if p:
            os.remove(p)


def fix_duration_fan():
    out = []
    for l in read_cfg("0011"):
        name, p = icon_fields(l)
        if name and int(p[2], 16) == ADDR_INTENSITY:
            p[5] = str(FAN_Y)
            l = "%s:%s" % (name, ",".join(p))
        out.append(l)
    write_cfg("0011", out)


def underlying(page, addr):
    for l in read_cfg(page):
        name, p = icon_fields(l)
        if name and int(p[2], 16) == addr:
            x, y, w, h = map(int, p[4:8])
            if x <= PILL_X and y <= PILL_Y and x + w >= PILL_X + PILL_W and y + h >= PILL_Y + PILL_H:
                return name, p
    raise SystemExit("Page%s: khong thay Icon 0x%04X" % (page, addr))


def under_image(page, addr, k, box):
    _, p = underlying(page, addr)
    x, y = int(p[4]), int(p[5])
    files = sorted(f for f in os.listdir(os.path.join(HMI, "Icon")) if f[:4].isdigit())
    f = files[files.index(p[8]) + k]
    bg = Image.open(os.path.join(HMI, "Picture", page_background(page))).convert("RGB").crop(box)
    im = Image.open(os.path.join(HMI, "Icon", f))
    rel = (box[0] - x, box[1] - y, box[2] - x, box[3] - y)
    if im.mode == "RGBA":
        bg.paste(im.crop(rel).convert("RGB"), (0, 0), im.crop(rel))
        return bg
    return im.convert("RGB").crop(rel)


def pill_mask():
    s = 4
    m = Image.new("L", (PILL_W * s, PILL_H * s), 0)
    from PIL import ImageDraw
    ImageDraw.Draw(m).rounded_rectangle((0, 0, PILL_W * s - 1, PILL_H * s - 1), radius=PILL_H * s // 2, fill=255)
    return m.resize((PILL_W, PILL_H), Image.LANCZOS)


def pill_message(src):
    a = np.array(src.convert("RGB")).astype(float)
    lum = a.mean(2)
    bg = lum[0, 0]
    ink_at = np.unravel_index(np.argmin(lum), lum.shape)
    ink = a[ink_at]
    alpha = np.clip((bg - lum) / max(bg - lum.min(), 1), 0, 1)[..., None]
    w = a.shape[1]
    out = np.tile(np.array(FILL, float), (PILL_H, PILL_W, 1))
    x0 = (PILL_W - w) // 2
    region = out[:, x0:x0 + w]
    out[:, x0:x0 + w] = region * (1 - alpha) + ink * alpha
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))


def clear_range(first, count):
    for f in glob.glob(os.path.join(HMI, "Icon", "*")):
        b = os.path.basename(f)
        if b[:4].isdigit() and first <= int(b[:4]) < first + count:
            os.remove(f)


def icon_line(k, addr, x, w, first, last, mn, mx):
    return "icon_%d:0xFFFF,2,0x%04X,2,%d,%d,%d,%d,%s,%s,,,%d,%d,icon_%d,Disable;" % (
        k, addr, x, PILL_Y, w, PILL_H, os.path.basename(icon_path(first)), os.path.basename(icon_path(last)), mn, mx, k)


def messages():
    sources = [Image.open(icon_path(MSG_FIRST + i)).convert("RGB") for i in range(MSG_COUNT)]
    pills = [pill_message(s) for s in sources]
    mask = pill_mask()
    clear_range(MSG_FIRST, MSG_COUNT)
    clear_range(1990, 2700 - 1990)
    box = (PILL_X, PILL_Y, PILL_X + PILL_W, PILL_Y + PILL_H)
    bg0 = Image.open(os.path.join(HMI, "Picture", page_background("0000"))).convert("RGB").crop(box)
    for i, pill in enumerate(pills):
        t, L = divmod(i, 6)
        im = bg0.copy()
        im.paste(pill, (0, 0), mask)
        save_jpg(im, os.path.join(HMI, "Icon", "%04d_message_%s_%s.jpg" % (MSG_FIRST + i, MSG_NAMES[t], LANGS[L])))
    out = []
    for l in read_cfg("0000"):
        name, p = icon_fields(l)
        if name and int(p[2], 16) == ADDR_MESSAGE:
            l = icon_line(int(name[5:]), ADDR_MESSAGE, PILL_X, PILL_W, MSG_FIRST, MSG_FIRST + MSG_COUNT - 1, 1, MSG_COUNT)
        out.append(l)
    write_cfg("0000", out)

    mid_box = (MID_X, PILL_Y, MID_X + MID_W, PILL_Y + PILL_H)
    cap_boxes = {"g": (PILL_X, PILL_Y, PILL_X + CAP_W, PILL_Y + PILL_H),
                 "d": (MID_X + MID_W, PILL_Y, PILL_X + PILL_W, PILL_Y + PILL_H)}
    for tag, cfg in SETS.items():
        page0, addr = cfg["pages"][0], cfg["addr"]
        for i, pill in enumerate(pills):
            t, L = divmod(i, 6)
            save_jpg(pill.crop((CAP_W, 0, CAP_W + MID_W, PILL_H)),
                     os.path.join(HMI, "Icon", "%04d_msg_%s_%s_%s.jpg" % (cfg["mid"] + i, tag, MSG_NAMES[t], LANGS[L])))
        for k in range(PIECES):
            save_jpg(under_image(page0, addr, k, mid_box),
                     os.path.join(HMI, "Icon", "%04d_msg_%s_piece_%02d.jpg" % (cfg["mid"] + MSG_COUNT + k, tag, k)))
        for side, first in (("g", cfg["cap_g"]), ("d", cfg["cap_d"])):
            cb = cap_boxes[side]
            rel = (cb[0] - PILL_X, 0, cb[2] - PILL_X, PILL_H)
            end, end_mask = pills[0].crop(rel), mask.crop(rel)
            for k in range(PIECES):
                under = under_image(page0, addr, k, cb)
                save_jpg(under, os.path.join(HMI, "Icon", "%04d_msg_%s_bord_%s_%02d.jpg" % (first + k, tag, side, k)))
                capped = under.copy()
                capped.paste(end, (0, 0), end_mask)
                save_jpg(capped, os.path.join(HMI, "Icon", "%04d_msg_%s_bord_%s_pill_%02d.jpg" % (first + PIECES + k, tag, side, k)))
        for page in cfg["pages"]:
            lines = [l for l in read_cfg(page)
                     if not (icon_fields(l)[0] and int(icon_fields(l)[1][2], 16) in (ADDR_MESSAGE, ADDR_MESSAGE_CAPS))]
            used = [int(m.group(1)) for l in lines for m in [re.match(r"icon_(\d+):", l)] if m]
            k = max(used) + 1
            new = [icon_line(k, ADDR_MESSAGE_CAPS, PILL_X, CAP_W, cfg["cap_g"], cfg["cap_g"] + 2 * PIECES - 1, 1, 2 * PIECES),
                   icon_line(k + 1, ADDR_MESSAGE, MID_X, MID_W, cfg["mid"], cfg["mid"] + MSG_COUNT + PIECES - 1, 1,
                             MSG_COUNT + PIECES),
                   icon_line(k + 2, ADDR_MESSAGE_CAPS, MID_X + MID_W, CAP_W, cfg["cap_d"], cfg["cap_d"] + 2 * PIECES - 1, 1,
                             2 * PIECES)]
            under, _ = underlying(page, addr)
            out = []
            for l in lines:
                if l.startswith(under + ":"):
                    out.extend(new)
                out.append(l)
            write_cfg(page, out)


if __name__ == "__main__":
    HMI = sys.argv[1]
    center_status()
    mode_icons()
    remove_titles()
    fix_duration_fan()
    messages()
    print("Xong: Statut canh giua, MODE WIFI / LOCAL / LAN, bo tieu de, sua 0011, thong bao bo tron o y %d" % PILL_Y)
