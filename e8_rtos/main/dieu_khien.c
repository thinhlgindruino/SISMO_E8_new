/*
 * dieu_khien.c - Task điều khiển (xem dieu_khien.h)
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "cau_hinh.h"
#include "hmi.h"
#include "cm8.h"
#include "nhat_ky.h"
#include "dieu_khien.h"

static const char *TEN_CHE_DO[] = {"CHI QUAT", "CHUAN BI", "CHAY", "DUNG"};
const char *dieu_khien_ten_che_do(che_do_cm8_t c) { return (c <= CM8_DUNG) ? TEN_CHE_DO[c] : "?"; }

static portMUX_TYPE khoa = portMUX_INITIALIZER_UNLOCKED;
static dieu_khien_trang_thai_t tt_chung = { .che_do = CM8_CHI_QUAT };

static che_do_cm8_t che_do = CM8_CHI_QUAT;
static int64_t lan_doi_che_do = 0;

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

dieu_khien_trang_thai_t dieu_khien_lay(void)
{
    portENTER_CRITICAL(&khoa);
    dieu_khien_trang_thai_t t = tt_chung;
    portEXIT_CRITICAL(&khoa);
    return t;
}

static bool la_trang_buoi_tap(int t) { return t == 6 || t == 8 || t == 10 || t == 11; }

// Cường độ 1..32 -> tốc độ CM8 0..32, làm tròn xuống số CHẴN
static uint16_t muc_sang_toc_do(int muc)
{
    if (muc <= 1) return 0;
    int v = (muc - 1) * TOC_DO_MAX / 31;
    if (v > TOC_DO_MAX) v = TOC_DO_MAX;
#if TOC_DO_CHI_SO_CHAN
    return (uint16_t)(v & ~1);     // CM8 đứng yên đúng mức, nhưng chỉ có 17 mức 0,2,4..32
#else
    return (uint16_t)v;            // đủ 33 mức; số lẻ thì CM8 dao động ±1 mức mỗi 50 ms quanh giá trị đó
#endif
}

static void doi_che_do(che_do_cm8_t moi)
{
    if (moi == che_do) return;
    che_do = moi;
    lan_doi_che_do = ms_now();
    nhat_ky_gui("   [CM8] -> %s", TEN_CHE_DO[moi]);
}

// Độ lệch Xung tính từ lúc bật Xung: tam giác 0,+1,+2,+1,0,-1,-2,-1 (biên độ 2)
static int64_t moc_xung = 0;
static int lech_xung(void)
{
    const int so_buoc = 4 * XUNG_BIEN_DO;
    int k = (int)(((ms_now() - moc_xung) / XUNG_BUOC_MS) % so_buoc);
    if (k <= XUNG_BIEN_DO)     return k;
    if (k <= 3 * XUNG_BIEN_DO) return 2 * XUNG_BIEN_DO - k;
    return k - 4 * XUNG_BIEN_DO;
}

static int gioi_han(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- Cường độ / Xung ----
// Bình thường: nút +/- và đồng hồ cùng dùng 0x1000, màn hình tự vẽ -> nhấn giữ mượt,
// ESP32 chỉ ĐỌC 0x1000. Khi đang tập + bật Xung: ESP32 ghi mức dao động vào 0x1000 (đồng hồ
// dao động theo), còn cường độ người dùng chọn giữ riêng trong cd_goc; tắt Xung thì ghi trả lại.
static int     cd_goc = 0;            // cường độ người dùng chọn 0..31
static bool    xung_hien = false;     // ESP32 đang ghi dao động vào 0x1000
static int     cd_da_ghi = -1;        // giá trị dao động ghi gần nhất
static int64_t cho_man_hinh_den = 0;  // đang chờ màn hình nhận giá trị trả lại

static void cap_nhat_cuong_do(const hmi_du_lieu_t *d)
{
    uint16_t v;
    while (hmi_lay_cham_cuong_do(&v)) {
        if (xung_hien) {
            // Màn hình cộng/trừ trên giá trị dao động đang hiện -> chỉ lấy chiều bấm
            int lech = (v > cd_da_ghi) ? 1 : ((int)v < cd_da_ghi ? -1 : 0);
            cd_goc = gioi_han(cd_goc + lech, 0, 31);
        }
    }
    if (!xung_hien) {
        if (cho_man_hinh_den && ((int)d->bien[0] == cd_goc || ms_now() >= cho_man_hinh_den))
            cho_man_hinh_den = 0;
        if (!cho_man_hinh_den) cd_goc = gioi_han(d->bien[0], 0, 31);
    }
}

static void task_dieu_khien(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    // ---- 1. Khởi động an toàn ----
    // Chỉ ĐỌC CM8 trước (chưa gửi lệnh) để biết CM8 vừa bật hay đã chạy từ trước.
    // Nếu CM8 đã có Mode (ESP32 vừa bị reset giữa chừng) thì KHÔNG dùng chế độ "chỉ quạt",
    // vì CM8 nhớ Mode -> lệnh 1 sẽ làm động cơ quay ở tốc độ thấp nhất.
    int64_t t0 = ms_now();
    cm8_trang_thai_t t = cm8_lay_trang_thai();
    while (t.so_lan_ok == 0 && ms_now() - t0 < 1500) {
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_task_wdt_reset();
        t = cm8_lay_trang_thai();
    }
    if (t.so_lan_ok > 0 && t.mode != 0) {
        che_do = CM8_DUNG;
        nhat_ky_gui("   [CM8] CM8 da co Mode=%u tu truoc (ESP32 vua khoi dong lai) -> DUNG cho an toan", t.mode);
    } else {
        che_do = CM8_CHI_QUAT;
        nhat_ky_gui("   [CM8] -> CHI QUAT (bat len la quat quay)");
    }
    lan_doi_che_do = ms_now();

    // Nếu ESP32 vừa khởi động lại vì lỗi (watchdog, crash) thì bắt màn hình tạm dừng buổi tập,
    // người dùng phải bấm ▶ lại -> động cơ không tự chạy lại khi chưa ai để ý.
    esp_reset_reason_t ly_do = esp_reset_reason();
    bool cho_dung_sau_loi = (ly_do == ESP_RST_TASK_WDT || ly_do == ESP_RST_INT_WDT ||
                             ly_do == ESP_RST_WDT || ly_do == ESP_RST_PANIC);
    const char *ly_do_dung = "ESP32 vua khoi dong lai do loi";
    int64_t lan_ghi_dung = 0;
    bool lien_lac_truoc = false, dang_tap_truoc = false;

    // ---- 2. Vòng điều khiển, chu kỳ đều 50 ms ----
    TickType_t moc = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&moc, pdMS_TO_TICKS(CHU_KY_DIEU_KHIEN_MS));
        esp_task_wdt_reset();

        hmi_du_lieu_t d = hmi_lay();
        t = cm8_lay_trang_thai();
        bool lien_lac = hmi_con_lien_lac(&d);

        // Mất liên lạc màn hình giữa buổi tập: khi có lại liên lạc cũng bắt tạm dừng,
        // không để động cơ tự chạy lại
        if (lien_lac_truoc && !lien_lac && dang_tap_truoc) {
            cho_dung_sau_loi = true;
            ly_do_dung = "Vua mat lien lac man hinh giua buoi tap";
            nhat_ky_gui("   [AN TOAN] MAT LIEN LAC man hinh -> dung dong co");
        }
        lien_lac_truoc = lien_lac;

        if (cho_dung_sau_loi && lien_lac) {
            if (d.bien[1] == 0) {
                cho_dung_sau_loi = false;
                nhat_ky_gui("   [AN TOAN] Man hinh da tam dung, bam > de tap tiep");
            } else if (ms_now() - lan_ghi_dung >= 500) {
                lan_ghi_dung = ms_now();
                hmi_ghi(0x1001, 0);
                nhat_ky_gui("   [AN TOAN] %s -> tam dung buoi tap tren man hinh", ly_do_dung);
            }
        }

        bool cho_chay = lien_lac && !cho_dung_sau_loi &&
                        (d.trang < 0 || la_trang_buoi_tap(d.trang)) && d.bien[1] == 1;

        // ---- Cường độ + Xung ----
        cap_nhat_cuong_do(&d);
        bool muon_xung = cho_chay && d.bien[5] == 1;
        if (muon_xung && !xung_hien) {
            xung_hien = true;
            cd_da_ghi = cd_goc;             // màn hình đang hiện đúng cd_goc
            moc_xung = ms_now();
        } else if (!muon_xung && xung_hien) {
            xung_hien = false;
            hmi_ghi(DIA_CHI_CUONG_DO, (uint16_t)cd_goc);          // trả lại cường độ đã chọn
            cho_man_hinh_den = ms_now() + CHO_MAN_HINH_MS;
        }
        int muc_dich = cho_chay ? gioi_han(cd_goc + 1 + (xung_hien ? lech_xung() : 0), 1, 32) : 0;
        if (xung_hien && muc_dich - 1 != cd_da_ghi) {
            cd_da_ghi = muc_dich - 1;
            hmi_ghi(DIA_CHI_CUONG_DO, (uint16_t)cd_da_ghi);       // đồng hồ dao động theo
        }
        dang_tap_truoc = cho_chay;
        bool cm8_ok = t.ket_noi && t.loi == 0;

        // ---- Chuyển chế độ ----
        switch (che_do) {
        case CM8_CHI_QUAT:
            if (cho_chay && cm8_ok) doi_che_do(CM8_CHUAN_BI);
            break;
        case CM8_CHUAN_BI:
            if (!cho_chay) doi_che_do(CM8_DUNG);
            else if ((t.lenh == CM8_LENH_DUNG && t.mode == MODE_CHAY && t.trang_thai == 0) ||
                     ms_now() - lan_doi_che_do > CHO_CHUAN_BI_MS)
                doi_che_do(CM8_CHAY);
            break;
        case CM8_CHAY:
            if (!cho_chay) doi_che_do(CM8_DUNG);
            break;
        case CM8_DUNG:
            if (cho_chay && cm8_ok) doi_che_do(CM8_CHAY);
            break;
        }

        // ---- Giá trị gửi CM8 (task cm8 tự gửi đi) ----
        uint16_t toc_do = 0;
        switch (che_do) {
        case CM8_CHI_QUAT:
            cm8_dat(CM8_LENH_CHAY, 0, GIOI_HAN_DONG, 0, 0);
            break;
        case CM8_CHUAN_BI:
        case CM8_DUNG:
            cm8_dat(CM8_LENH_DUNG, 0, GIOI_HAN_DONG, MODE_CHAY, RATIO_CHAY);
            break;
        case CM8_CHAY:
            toc_do = muc_sang_toc_do(muc_dich);
            cm8_dat(CM8_LENH_CHAY, toc_do, GIOI_HAN_DONG, MODE_CHAY, RATIO_CHAY);
            break;
        }

        portENTER_CRITICAL(&khoa);
        tt_chung.che_do = che_do;
        tt_chung.toc_do_gui = toc_do;
        tt_chung.muc_dich = muc_dich;
        tt_chung.cho_chay = cho_chay;
        tt_chung.cuong_do = cd_goc;
        portEXIT_CRITICAL(&khoa);

        // ---- CM8 lỗi / mất kết nối khi đang tập -> màn hình tạm dừng ----
        static int64_t lan_bao_loi = 0;
        if (cho_chay && !cm8_ok && ms_now() - lan_bao_loi >= 1000) {
            lan_bao_loi = ms_now();
            if (!t.ket_noi) nhat_ky_gui("   [CM8] KHONG PHAN HOI - kiem tra nguon 220V, day, module SP3232");
            else            nhat_ky_gui("   [CM8] LOI %u (%s) - tat/bat lai nguon CM8", t.loi, cm8_ten_loi(t.loi));
#if DUNG_MAN_HINH_KHI_LOI
            hmi_ghi(0x1001, 0);
            nhat_ky_gui("   [CM8] -> tam dung buoi tap tren man hinh");
#endif
        }

        // ---- Báo khi kết nối / lỗi CM8 thay đổi ----
        static int ket_noi_truoc = -1, loi_truoc = -1;
        if ((int)t.ket_noi != ket_noi_truoc || (t.ket_noi && (int)t.loi != loi_truoc)) {
            if (t.ket_noi) nhat_ky_gui("   [CM8] Ket noi OK | Loi %u (%s)", t.loi, cm8_ten_loi(t.loi));
            else if (ket_noi_truoc != -1) nhat_ky_gui("   [CM8] MAT KET NOI");
            ket_noi_truoc = t.ket_noi;
            loi_truoc = t.loi;
        }
    }
}

void dieu_khien_khoi_dong(void)
{
    xTaskCreatePinnedToCore(task_dieu_khien, "dieu_khien", STACK_DIEU_KHIEN, NULL,
                            UU_TIEN_DIEU_KHIEN, NULL, CORE_UNG_DUNG);
}
