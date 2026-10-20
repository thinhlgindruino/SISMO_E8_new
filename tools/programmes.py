"""
programmes.py - Số liệu 54 bài Programmes + 6 bài Perso của E8 (đọc từ ảnh chụp máy E8 cũ) + bộ sinh file

Sửa số liệu ở PROGRAMS bên dưới rồi chạy:
    python programmes.py <thư mục HMI_E8> <thư mục e8_rtos>
-> sinh lại ảnh biểu đồ (Icon 0800.. xám, 0860.. xanh: mỗi biểu đồ khác nhau 1 ảnh, bài-mức trùng biểu đồ
   dùng chung; 0920 + mức - 1 cột đều cho Xung, chỉ các mức cần dùng), sửa dải icon giữ ảnh trong
   Plugin/Page0006.cfg, Page0014.cfg, ghi components/pages/page_program/program_data.c và in bảng kiểm tra.

Quy ước:
  - Biểu đồ có 120 vị trí cột: trục 10 phút -> mỗi cột 5 s; trục 2 phút (bài <= 2 phút) -> mỗi cột 1 s.
  - Đoạn cột: (số cột, mức cao, mức thấp). Mức thấp = 0 -> mọi cột cùng mức cao; khác 0 -> xen kẽ
    cao / thấp, bắt đầu bằng cột cao (bài Tonification).
  - Bài tập con: (giây, số hình minh họa 1..32 = icon 0500..0531).
  - Xung: các cột đã / đang sáng dao động ±3 quanh mức cao của đoạn hiện tại.
"""
import os, sys

def alt(n, hi, lo):
    return [(n, hi, lo)]

def rep(times, *segs):
    out = []
    for _ in range(times):
        out += list(segs)
    return out

GRID_POITRINE = [(60, 5), (60, 7), (60, 3), (60, 5), (60, 7)]
CUISSES = [26, 15, 26, 15, 26, 15, 26, 15, 26, 15, 26, 18, 20]
SUB_CUISSES = [(40, i) for i in CUISSES[:11]] + [(80, CUISSES[11]), (80, CUISSES[12])]

PROGRAMS = [
    dict(name="Tonification - Bras", x=True, grid=(2, 0), secs=(300, 300, 300),
         sub=[(40, 24), (20, 23), (40, 24), (20, 23), (60, 24), (20, 23), (80, 24), (20, 23)],
         prof=[[(60, 18, 6)], [(60, 22, 8)], [(60, 25, 10)]]),
    dict(name="Tonification - Poitrine Bras", x=True, grid=(1, 0), secs=(300, 300, 300), sub=GRID_POITRINE,
         prof=[[(36, 16, 5), (24, 20, 7)], [(36, 19, 7), (24, 23, 8)], [(36, 23, 9), (24, 27, 11)]]),
    dict(name="Tonification - Sangle abdominale", x=True, grid=(6, 0), secs=(600, 600, 600),
         sub=[(40, i) for i in [20, 26, 20, 26, 20, 26, 20, 26, 20, 26, 20, 26, 20, 26, 27]],
         prof=[[(120, 18, 6)], [(48, 21, 8), (72, 22, 9)], [(120, 25, 10)]]),
    dict(name="Tonification - Cuisses Fessiers", x=True, grid=(7, 0), secs=(600, 600, 600), sub=SUB_CUISSES,
         prof=[rep(5, (8, 19, 7), (8, 23, 9)) + [(40, 19, 7)],
               rep(5, (8, 23, 7), (8, 27, 10)) + [(40, 23, 7)],
               rep(5, (8, 25, 10), (8, 29, 12)) + [(40, 25, 10)]]),
    dict(name="Tonification - Complet", x=True, grid=(4, 0), secs=(600, 600, 600),
         sub=rep(3, (90, 25), (45, 26), (45, 30)) + [(30, 12), (30, 13)],
         prof=[rep(3, (18, 23, 9), (18, 19, 7)) + [(12, 17, 6)],
               rep(3, (18, 27, 11), (18, 23, 9)) + [(12, 19, 7)],
               rep(3, (18, 30, 13), (18, 27, 11)) + [(12, 23, 9)]]),
    dict(name="Remodelage - Poitrine Bras", x=True, grid=(1, 0), secs=(300, 300, 300), sub=GRID_POITRINE,
         prof=[[(36, 14, 0), (24, 21, 0)], [(36, 18, 0), (24, 25, 0)], [(36, 22, 0), (24, 29, 0)]]),
    dict(name="Remodelage - Sangle abdominale", x=True, grid=(8, 0), secs=(600, 600, 600),
         sub=[(40, i) for i in [26, 20, 27, 22, 21, 22, 21, 22, 21, 22, 21]] + [(80, 22), (80, 21)],
         prof=[[(120, 18, 0)], [(120, 22, 0)], [(120, 25, 0)]]),
    dict(name="Remodelage - Cuisses Fessiers 1", x=True, grid=(9, 0), secs=(600, 600, 600),
         sub=[(40, i) for i in [26, 16, 19, 16, 19, 16, 19, 16, 19, 16, 19]] + [(80, 18), (80, 20)],
         prof=[[(120, 18, 0)], [(120, 22, 0)], [(120, 25, 0)]]),
    dict(name="Remodelage - Cuisses Fessiers 2", x=True, grid=(7, 0), secs=(600, 600, 600), sub=SUB_CUISSES,
         prof=[rep(5, (8, 17, 0), (8, 21, 0)) + [(40, 17, 0)],
               rep(5, (8, 21, 0), (8, 25, 0)) + [(40, 21, 0)],
               rep(5, (8, 25, 0), (8, 29, 0)) + [(40, 25, 0)]]),
    dict(name="Remodelage - Complet", x=True, grid=(5, 3), secs=(600, 600, 600),
         sub=[(30, i) for i in [25, 26, 25, 26, 25, 22, 21, 22, 21, 26]] + [(60, 27), (30, 17)] +
             [(30, i) for i in [18, 20, 10, 12, 8, 4, 6]],
         prof=[[(6, 19, 0), (6, 13, 0), (6, 19, 0), (6, 13, 0), (6, 19, 0), (60, 13, 0), (18, 9, 0), (12, 23, 0)],
               [(6, 23, 0), (6, 17, 0), (6, 23, 0), (6, 17, 0), (6, 23, 0), (60, 17, 0), (18, 13, 0), (12, 27, 0)],
               [(6, 27, 0), (6, 21, 0), (6, 27, 0), (6, 21, 0), (6, 27, 0), (60, 21, 0), (18, 17, 0), (12, 30, 0)]]),
    dict(name="Anti-cellulite - Cuisses Fessiers", x=True, grid=(10, 0), secs=(600, 600, 600),
         sub=rep(5, (60, 26), (30, 18), (30, 25)),
         prof=[rep(5, (12, 18, 0), (12, 21, 0)), rep(5, (12, 21, 0), (12, 25, 0)), rep(5, (12, 25, 0), (12, 29, 0))]),
    dict(name="Gainage - Complet 1", x=False, grid=(0, 0), secs=(120, 300, 600), sub=[(0, 29)],
         prof=[[(30, 25, 0), (30, 21, 0), (30, 18, 0), (30, 14, 0)],
               [(12, 23, 0), (12, 19, 0), (12, 16, 0), (12, 12, 0), (12, 8, 0)],
               [(24, 11, 0), (24, 8, 0), (24, 12, 0), (24, 8, 0), (24, 12, 0)]]),
    dict(name="Gainage - Complet 2", x=False, grid=(0, 0), secs=(120, 300, 600), sub=[(0, 13)],
         prof=[[(120, 10, 0)], [(60, 10, 0)], [(120, 10, 0)]]),
    dict(name="Relaxation Drainage - Drainage", x=False, grid=(0, 0), secs=(300, 300, 300), sub=[(0, 17)],
         prof=[[(60, 23, 0)], [(60, 27, 0)], [(60, 31, 0)]]),
    dict(name="Relaxation Drainage - Relax 1", x=False, grid=(0, 0), secs=(300, 300, 300), sub=[(0, 14)],
         prof=[[(13, b, 0), (2, s, 0), (12, b, 0), (2, s, 0), (12, b, 0), (2, s, 0), (17, b, 0)]
               for b, s in ((14, 20), (17, 23), (21, 27))]),
    dict(name="Relaxation Drainage - Relax 2", x=False, grid=(0, 0), secs=(300, 300, 300), sub=[(0, 28)],
         prof=[[(60, 25, 0)], rep(5, (6, 17, 0), (6, 21, 0)),
               [(4, 23, 0), (4, 17, 0), (4, 23, 0), (4, 17, 0), (4, 23, 0), (20, 17, 0), (12, 12, 0), (8, 27, 0)]]),
    dict(name="Proprioception - 1", x=False, grid=(0, 0), secs=(60,), sub=[(0, 32)], prof=[rep(3, (10, 10, 0), (10, 29, 0))]),
    dict(name="Proprioception - 2", x=False, grid=(0, 0), secs=(120,), sub=[(0, 32)], prof=[rep(6, (10, 10, 0), (10, 19, 0))]),
    dict(name="Proprioception - 3", x=False, grid=(0, 0), secs=(180,), sub=[(0, 32)], prof=[rep(6, (2, 9, 0), (2, 19, 0), (2, 28, 0))]),
    dict(name="Proprioception - 4", x=False, grid=(0, 0), secs=(300,), sub=[(0, 32)], prof=[rep(5, (4, 18, 0), (4, 22, 0)) + [(20, 18, 0)]]),
    dict(name="Proprioception - 5", x=False, grid=(0, 0), secs=(600,), sub=[(0, 32)], prof=[rep(5, (12, 21, 0), (12, 25, 0))]),
    dict(name="Proprioception - 6", x=False, grid=(0, 0), secs=(600,), sub=[(0, 32)],
         prof=[[(6, 23, 0), (6, 17, 0), (6, 23, 0), (6, 17, 0), (6, 23, 0), (60, 17, 0), (18, 14, 0), (12, 27, 0)]]),
    dict(name="Perso - 1", x=False, grid=(0, 0), secs=(60,), sub=[(0, 32)], prof=[[(60, 10, 0)]]),
    dict(name="Perso - 2", x=False, grid=(0, 0), secs=(120,), sub=[(0, 32)], prof=[[(60, 10, 0), (60, 8, 0)]]),
    dict(name="Perso - 3", x=False, grid=(0, 0), secs=(180,), sub=[(0, 32)], prof=[[(12, 10, 0), (12, 8, 0), (12, 6, 0)]]),
    dict(name="Perso - 4", x=False, grid=(0, 0), secs=(60,), sub=[(0, 32)], prof=[[(60, 16, 0)]]),
    dict(name="Perso - 5", x=False, grid=(0, 0), secs=(120,), sub=[(0, 32)], prof=[[(60, 16, 0), (60, 14, 0)]]),
    dict(name="Perso - 6", x=False, grid=(0, 0), secs=(180,), sub=[(0, 32)], prof=[[(12, 16, 0), (12, 14, 0), (12, 12, 0)]]),
]

LEVELS = ("Débutant", "Initié", "Confirmé")
CHART_W, CHART_H, PX_LEVEL, SLOTS = 480, 192, 6, 120
BG, GRAY, GREEN = (14, 17, 26), (190, 196, 212), (60, 230, 40)
ICON_GRAY, ICON_GREEN, ICON_COMB = 800, 860, 920
PULSE = 3

def bar_seconds(secs):
    return 1 if secs <= 120 else 5

def bars(profile):
    out = []
    for n, hi, lo in profile:
        for j in range(n):
            out.append((lo if (lo and j % 2) else hi, hi))
    return out

def entries():
    k = 0
    for p, prog in enumerate(PROGRAMS):
        for lv in range(len(prog["secs"])):
            yield k, p, lv, prog
            k += 1

def check():
    errors = []
    for k, p, lv, prog in entries():
        secs = prog["secs"][lv]
        n = secs // bar_seconds(secs)
        got = sum(s[0] for s in prog["prof"][lv])
        if got != n:
            errors.append("%s %s: %d cot, phai %d" % (prog["name"], LEVELS[lv], got, n))
        for _, hi, lo in prog["prof"][lv]:
            if not (1 <= hi <= 32) or lo < 0 or lo > 32:
                errors.append("%s %s: muc ngoai 1..32" % (prog["name"], LEVELS[lv]))
        if prog["sub"][0][0]:
            tot = sum(s for s, _ in prog["sub"])
            if tot != secs:
                errors.append("%s: bai tap con %d s, phai %d s" % (prog["name"], tot, secs))
    return errors

def chart(levels, color):
    from PIL import Image, ImageDraw
    im = Image.new("RGB", (CHART_W, CHART_H), BG)
    d = ImageDraw.Draw(im)
    for i, lv in enumerate(levels):
        if lv > 0:
            d.rectangle([4 * i, CHART_H - lv * PX_LEVEL, 4 * i + 1, CHART_H - 1], fill=color)
    return im

def charts():
    index, uniq = {}, []
    for k, p, lv, prog in entries():
        key = tuple(v for v, _ in bars(prog["prof"][lv]))
        if key not in index:
            index[key] = len(uniq)
            uniq.append(key)
        yield k, index[key], uniq

def chart_map():
    m, uniq = {}, []
    for k, u, uniq in charts():
        m[k] = u
    return m, uniq

def comb_min():
    low = 32
    for k, p, lv, prog in entries():
        for n, hi, lo in prog["prof"][lv]:
            low = min(low, max(1, hi - PULSE))
    return low

def write_images(hmi):
    icon = os.path.join(hmi, "Icon")
    for f in os.listdir(icon):
        if f[:4].isdigit() and (ICON_GRAY <= int(f[:4]) < ICON_GRAY + 60 or ICON_GREEN <= int(f[:4]) < ICON_GREEN + 60 or ICON_COMB <= int(f[:4]) < ICON_COMB + 32):
            os.remove(os.path.join(icon, f))
    m, uniq = chart_map()
    for u, lvls in enumerate(uniq):
        chart(lvls, GRAY).save(os.path.join(icon, "%04d_profil_gris_%02d.png" % (ICON_GRAY + u, u + 1)))
        chart(lvls, GREEN).save(os.path.join(icon, "%04d_profil_vert_%02d.png" % (ICON_GREEN + u, u + 1)))
    for lv in range(comb_min(), 33):
        chart([lv] * SLOTS, GREEN).save(os.path.join(icon, "%04d_peigne_%02d.png" % (ICON_COMB + lv - 1, lv)))

def update_cfg(hmi):
    m, uniq = chart_map()
    n, low = len(uniq), comb_min()
    gray = ("%04d_profil_gris_01.png" % ICON_GRAY, "%04d_profil_gris_%02d.png" % (ICON_GRAY + n - 1, n))
    green = ("%04d_profil_vert_01.png" % ICON_GREEN, "%04d_profil_vert_%02d.png" % (ICON_GREEN + n - 1, n))
    comb = ("%04d_peigne_%02d.png" % (ICON_COMB + low - 1, low), "%04d_peigne_32.png" % (ICON_COMB + 31))
    for page in ("Page0006.cfg", "Page0014.cfg"):
        path = os.path.join(hmi, "Plugin", page)
        with open(path, encoding="utf-8-sig") as f:
            lines = f.read().splitlines()
        out = []
        for l in lines:
            f = l.split(",")
            if l.startswith("icon_") and len(f) > 14 and f[2] == "0x105D":
                pair = gray if "profil_gris" in f[8] else green if "profil_vert" in f[8] else comb
                f[8], f[9], f[12], f[13] = pair[0], pair[1], "1", str(n if pair is not comb else 33 - low)
                l = ",".join(f)
            elif l.startswith("progress_0:"):
                f[9], f[13] = green[0], gray[0]
                l = ",".join(f)
            out.append(l)
        with open(path, "w", encoding="utf-8-sig", newline="") as f:
            f.write("\n".join(out) + "\n")

def c_string(s):
    return s.encode("ascii", "replace").decode()

def write_c(rtos):
    path = os.path.join(rtos, "components", "pages", "page_program", "program_data.c")
    L = []
    L.append("/*")
    L.append(" * program_data.c - Số liệu 54 bài Programmes + 6 bài Perso (SINH TỰ ĐỘNG bởi tools/programmes.py, không sửa tay)")
    L.append(" *")
    L.append(" * Mỗi bài: tên, có nút X, lưới bài tập con, thời lượng / mức độ, biểu đồ / mức độ (đoạn cột:")
    L.append(" * số cột, mức cao, mức thấp; mức thấp 0 = cùng mức), danh sách bài tập con (giây, hình 1..32).")
    L.append(" * Số thứ tự biểu đồ = icon 0800 + n (xám), 0860 + n (xanh); bài-mức có cùng biểu đồ dùng chung 1 ảnh.")
    L.append(" */")
    L.append('#include "program_data.h"')
    L.append("")
    for k, p, lv, prog in entries():
        L.append("static const program_seg_t SEG_%02d[] = { %s };" % (k, ", ".join("{ %d, %d, %d }" % s for s in prog["prof"][lv])))
    L.append("")
    for p, prog in enumerate(PROGRAMS):
        L.append("static const program_sub_t SUB_%02d[] = { %s };" % (p, ", ".join("{ %d, %d }" % s for s in prog["sub"])))
    L.append("")
    cmap, uniq = chart_map()
    L.append("const uint8_t PROGRAM_COMB_MIN = %d;" % comb_min())
    L.append("")
    L.append("const program_info_t PROGRAM_INFO[PROGRAM_COUNT] = {")
    k = 0
    for p, prog in enumerate(PROGRAMS):
        nl = len(prog["secs"])
        secs = list(prog["secs"]) + [prog["secs"][-1]] * (3 - nl)
        profs = []
        for lv in range(3):
            kk = k + min(lv, nl - 1)
            profs.append("{ SEG_%02d, %d, %d }" % (kk, len(prog["prof"][min(lv, nl - 1)]), cmap[kk]))
        L.append('    { "%s", %s, %d, %d, { %s }, { %s }, SUB_%02d, %d },' % (
            c_string(prog["name"]), "true" if prog["x"] else "false", prog["grid"][0], prog["grid"][1],
            ", ".join(str(s) for s in secs), ", ".join(profs), p, len(prog["sub"])))
        k += nl
    L.append("};")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")

def table():
    rows = []
    for k, p, lv, prog in entries():
        secs = prog["secs"][lv]
        segs = "; ".join(("%d×%d/%d" % (n, hi, lo)) if lo else ("%d×%d" % (n, hi)) for n, hi, lo in prog["prof"][lv])
        name = prog["name"] + ("" if p >= 16 else " - " + LEVELS[lv])
        rows.append("| %d | %s | %d:%02d | %ds | %s |" % (k + 1, name, secs // 60, secs % 60, bar_seconds(secs), segs))
    return rows

if __name__ == "__main__":
    errs = check()
    if errs:
        print("\n".join(errs)); sys.exit(1)
    if len(sys.argv) >= 3:
        write_images(sys.argv[1])
        update_cfg(sys.argv[1])
        write_c(sys.argv[2])
        print("%d biểu đồ khác nhau / %d bài-mức, cột đều Xung từ mức %d" % (len(chart_map()[1]), sum(1 for _ in entries()), comb_min()))
    print("\n".join(table()))
