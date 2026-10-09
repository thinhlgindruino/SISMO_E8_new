/*
 * cai_dat.c - Task cài đặt (xem cai_dat.h)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "cau_hinh.h"
#include "hmi.h"
#include "mang.h"
#include "dieu_khien.h"
#include "nhat_ky.h"
#include "cai_dat.h"

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

// ---- Giữ "Sismo" ----
static bool    dang_giu = false;
static bool    da_vao_mat_khau = false;
static int64_t giu_tu = 0, lan_lap_cuoi = 0, bo_qua_den = 0;

// ---- Mật khẩu ----
static char so_nhap[MAT_KHAU_SO_KY_TU + 1];
static int  so_ky_tu = 0;

static void hien_dau_cham(void) { hmi_ghi(DC_SO_DAU_CHAM, (uint16_t)so_ky_tu); }

static void xoa_mat_khau(void)
{
    so_ky_tu = 0;
    so_nhap[0] = '\0';
    hien_dau_cham();
}

static void chuyen_trang(int trang)
{
    hmi_ghi(0x7000, (uint16_t)trang);
}

static uint16_t ma_icon_mang(mang_trang_thai_t t)
{
    switch (t) {
    case MANG_CAU_HINH: return 1;
    case MANG_ONLINE:   return 2;
    default:            return 0;   // OFFLINE và đang kết nối đều hiện OFFLINE
    }
}

// ---------------- Xử lý từng loại nút ----------------
static void bam_sismo(int trang, const hmi_su_kien_t *e)
{
#if IN_SU_KIEN_SISMO
    static int64_t truoc = 0;
    int64_t t = ms_now();
    nhat_ky_gui("   [SISMO] %s gia tri %u | trang %d | cach lan truoc %lld ms",
                e->nguon == SK_NGUON_DOC ? "doc thay doi" : "man hinh bao 0x41",
                e->gia_tri, trang, (long long)(t - truoc));
    truoc = t;
#endif
    int64_t bay_gio = ms_now();
    if (bay_gio < bo_qua_den) return;           // thay đổi còn sót ngay sau lúc thả tay

    // Màn hình báo 0x41 lúc THẢ TAY -> kết thúc lần giữ ngay (không chờ hết NHA_TAY_MS).
    // (Nếu 0x41 tới ngay đầu lần bấm thì giu_tu mới vài chục ms -> vẫn coi là đang bấm.)
    if (e->nguon == SK_NGUON_BAO && dang_giu && bay_gio - giu_tu > 300) {
        dang_giu = false;
        bo_qua_den = bay_gio + BO_QUA_SAU_THA_MS;
        return;
    }

    lan_lap_cuoi = bay_gio;
    if (dang_giu) return;                       // đang giữ: chỉ cập nhật thời điểm lặp
    dang_giu = true;
    da_vao_mat_khau = false;
    giu_tu = lan_lap_cuoi;
    if (trang != TRANG_CHINH) {
        chuyen_trang(TRANG_CHINH);              // chạm ở trang khác -> về trang chính ngay
        nhat_ky_gui("   [CAI DAT] Cham Sismo -> ve trang chinh");
    }
}

static void bat_cau_hinh(const char *ly_do);

// Giống E8 cũ: không có nút xác nhận.
//   - Đủ 5 số mà đúng 74700 -> tự sang trang cài đặt
//   - Đủ 5 số mà sai -> đứng yên, bấm thêm 1 số (số thứ 6) -> xóa hết, nhập lại từ đầu
//   - C -> xóa hết
static void bam_phim_mat_khau(uint16_t phim, int trang)
{
#if IN_SU_KIEN_SISMO
    nhat_ky_gui("   [PHIM] %u | trang %d", phim, trang);
#endif
    if (trang != TRANG_MAT_KHAU) return;
    if (phim == PHIM_C) { xoa_mat_khau(); return; }
    if (phim > 9) return;

    if (so_ky_tu >= MAT_KHAU_SO_KY_TU) {        // số thứ 6 -> reset
        nhat_ky_gui("   [CAI DAT] Mat khau sai -> xoa, nhap lai");
        xoa_mat_khau();
        return;
    }
    so_nhap[so_ky_tu++] = (char)('0' + phim);
    so_nhap[so_ky_tu] = '\0';
    hien_dau_cham();

    if (so_ky_tu == MAT_KHAU_SO_KY_TU && strcmp(so_nhap, MAT_KHAU_CAI_DAT) == 0) {
        nhat_ky_gui("   [CAI DAT] Mat khau dung -> trang cai dat");
        xoa_mat_khau();
        // Giống E8 cũ: đã cấu hình WiFi rồi thì vào là "Configuration WIFI" luôn (bật AP ngay);
        // chưa cấu hình thì hiện OFFLINE, bấm vào mới bật.
        char ssid[33];
        mang_lay_thong_tin(ssid, sizeof(ssid), NULL, 0);
        if (ssid[0]) bat_cau_hinh("da co WiFi luu");
        else         hmi_ghi(DC_TRANG_THAI_MANG, ma_icon_mang(mang_trang_thai()));
        chuyen_trang(TRANG_CAI_DAT);
    }
}

static void bat_cau_hinh(const char *ly_do)
{
    if (mang_trang_thai() == MANG_CAU_HINH) return;             // đang cấu hình rồi
    if (dieu_khien_lay().che_do == CM8_CHAY) {  // phòng hờ: không bật AP khi động cơ còn chạy
        nhat_ky_gui("   [CAI DAT] Dong co dang chay -> khong vao che do cau hinh");
        return;
    }
    hmi_ghi(DC_TRANG_THAI_MANG, 1);              // hiện "Configuration WIFI" ngay
    nhat_ky_gui("   [CAI DAT] Bat che do cau hinh (%s): AP %s, mat khau %s, http://%s",
                ly_do, mang_ten_ap(), MANG_AP_MAT_KHAU, MANG_AP_IP);
    mang_bat_cau_hinh();
}

static void bam_nut_cai_dat(int trang)
{
    if (trang != TRANG_CAI_DAT) return;
    bat_cau_hinh("bam OFFLINE");
}

// ---------------- Task ----------------
static void task_cai_dat(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    int     trang_truoc = -1;
    int64_t lan_ghi_mang = 0, roi_trang_tu = 0;
    int     icon_da_ghi = -1;
    mang_trang_thai_t mang_truoc = (mang_trang_thai_t)-1;

    while (1) {
        hmi_su_kien_t e;
        bool co = hmi_lay_su_kien(&e, pdMS_TO_TICKS(50));
        esp_task_wdt_reset();

        hmi_du_lieu_t d = hmi_lay();
        int trang = d.trang;

        if (co) {
            if      (e.dia_chi == DC_NUT_SISMO)     bam_sismo(trang, &e);
            else if (e.dia_chi == DC_PHIM_MAT_KHAU) bam_phim_mat_khau(e.gia_tri, trang);
            else if (e.dia_chi == DC_NUT_CAI_DAT)   bam_nut_cai_dat(trang);
        }

        int64_t now = ms_now();

        // ---- Giữ "Sismo": thả tay hay đủ 3 s? ----
        if (dang_giu) {
            if (now - lan_lap_cuoi > NHA_TAY_MS) {
                dang_giu = false;                                   // đã thả tay
            } else if (!da_vao_mat_khau && now - giu_tu >= GIU_SISMO_MS) {
                da_vao_mat_khau = true;
                xoa_mat_khau();
                chuyen_trang(TRANG_MAT_KHAU);
                nhat_ky_gui("   [CAI DAT] Giu Sismo %d ms -> trang mat khau", GIU_SISMO_MS);
            }
        }

        // ---- Vừa vào trang mật khẩu (kể cả bằng nút thường) -> xóa ô nhập ----
        if (trang != trang_truoc) {
            if (trang == TRANG_MAT_KHAU) xoa_mat_khau();
            if (trang == TRANG_CAI_DAT)  icon_da_ghi = -1;          // ghi lại icon trạng thái
            trang_truoc = trang;
        }

        // ---- Icon trạng thái mạng: ghi khi đổi + làm mới mỗi 2 s ----
        mang_trang_thai_t m = mang_trang_thai();
        if (m != mang_truoc) {
            nhat_ky_gui("   [MANG] %s", mang_ten_trang_thai(m));
            mang_truoc = m;
        }
        int icon = ma_icon_mang(m);
        if (icon != icon_da_ghi || now - lan_ghi_mang >= 2000) {
            hmi_ghi(DC_TRANG_THAI_MANG, (uint16_t)icon);
            icon_da_ghi = icon;
            lan_ghi_mang = now;
        }

        // ---- Rời trang cài đặt khi đang cấu hình (quá 1 s) -> khởi động lại về chế độ thường ----
        if (m == MANG_CAU_HINH && trang >= 0 && trang != TRANG_CAI_DAT) {
            if (!roi_trang_tu) roi_trang_tu = now;
            else if (now - roi_trang_tu > 1000) {
                nhat_ky_gui("   [CAI DAT] Roi trang cai dat -> thoat che do cau hinh (khoi dong lai)");
                mang_khoi_dong_lai_sau(300);
                roi_trang_tu = now + 100000;                         // không báo lại
            }
        } else {
            roi_trang_tu = 0;
        }
    }
}

void cai_dat_khoi_dong(void)
{
    xTaskCreatePinnedToCore(task_cai_dat, "cai_dat", STACK_CAI_DAT, NULL,
                            UU_TIEN_CAI_DAT, NULL, CORE_UNG_DUNG);
}
