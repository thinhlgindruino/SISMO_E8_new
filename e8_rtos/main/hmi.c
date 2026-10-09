/*
 * hmi.c - Task nhận / task hỏi màn hình HMI (xem hmi.h)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "cau_hinh.h"
#include "hmi.h"
#include "nhat_ky.h"

static SemaphoreHandle_t khoa;          // mutex bảo vệ 'dl'
static hmi_du_lieu_t dl = { .trang = -1 };

// Hàng đợi các lần người dùng bấm +/- (Touch Returned Message của biến 0x1000).
// Task điều khiển cần biết từng lần bấm khi đang Xung (lúc đó 0x1000 do ESP32 ghi dao động).
static QueueHandle_t hang_doi_cham;
static QueueHandle_t hang_doi_su_kien;   // bấm nút của phần cài đặt

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }

static uint16_t crc16_modbus(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : (c >> 1);
    }
    return c;
}

// Khung 10 byte: 5A A5 07 | lệnh | addr H L | data H L | CRC L H
// uart_write_bytes của ESP-IDF có khóa riêng nên nhiều task cùng gửi không bị xen byte.
static void gui_khung(uint8_t lenh, uint16_t dia_chi, uint16_t gia_tri)
{
    uint8_t f[10] = {0x5A, 0xA5, 0x07, lenh,
                     (uint8_t)(dia_chi >> 8), (uint8_t)dia_chi,
                     (uint8_t)(gia_tri >> 8), (uint8_t)gia_tri, 0, 0};
    uint16_t c = crc16_modbus(&f[3], 5);
    f[8] = c & 0xFF;
    f[9] = c >> 8;
    uart_write_bytes(HMI_UART, (const char *)f, sizeof(f));
}

void hmi_ghi(uint16_t dia_chi, uint16_t gia_tri) { gui_khung(0x10, dia_chi, gia_tri); }

bool hmi_lay_cham_cuong_do(uint16_t *gia_tri)
{
    return xQueueReceive(hang_doi_cham, gia_tri, 0) == pdTRUE;
}

bool hmi_lay_su_kien(hmi_su_kien_t *e, TickType_t cho)
{
    return xQueueReceive(hang_doi_su_kien, e, cho) == pdTRUE;
}

hmi_du_lieu_t hmi_lay(void)
{
    hmi_du_lieu_t d;
    xSemaphoreTake(khoa, portMAX_DELAY);
    d = dl;
    xSemaphoreGive(khoa);
    return d;
}

bool hmi_con_lien_lac(const hmi_du_lieu_t *d)
{
    return d->co_bien && (ms_now() - d->lan_nhan_cuoi_ms < HMI_MAT_KET_NOI_MS);
}

// ---------------- Chẩn đoán: in mỗi lần cường độ đổi ----------------
static void bao_doi_cuong_do(uint16_t cu, uint16_t moi, const char *nguon)
{
#if IN_DOI_CUONG_DO
    static int64_t lan_truoc = 0;
    int64_t bay_gio = ms_now();
    if (cu != moi) {
        nhat_ky_gui("   [HMI] cuong do %u -> %u (%+d) | cach lan truoc %lld ms | %s",
                    cu + 1, moi + 1, (int)moi - (int)cu, (long long)(bay_gio - lan_truoc), nguon);
        lan_truoc = bay_gio;
    }
#else
    (void)cu; (void)moi; (void)nguon;
#endif
}

// ---------------- Giải mã khung nhận được ----------------
static void xu_ly_khung(const uint8_t *p, uint16_t n)
{
    if (n < 3) return;
    uint16_t crc_nhan = p[n - 2] | (p[n - 1] << 8);
    if (crc_nhan != crc16_modbus(p, n - 2)) return;

    // Kết quả đọc: 03 | addr(2) | số word(2) | data | CRC
    if (p[0] == 0x03 && n >= 7) {
        uint16_t dia_chi = (p[1] << 8) | p[2];
        uint16_t so_word = (p[3] << 8) | p[4];
        if (n != 5 + 2 * so_word + 2) return;
        // 0x1011 (phím mật khẩu) + 0x1012 (nút Sismo), đọc chung 1 khung
        if (dia_chi == DC_PHIM_MAT_KHAU && so_word == 2) {
            uint16_t phim  = (p[5] << 8) | p[6];
            uint16_t sismo = (p[7] << 8) | p[8];

            // Phím: ESP32 ghi PHIM_TRONG sau mỗi lần đọc được phím -> bấm lại cùng phím vẫn thấy đổi.
            // Nhanh hơn chờ 0x41 (màn hình chỉ báo 0x41 lúc thả tay).
            static bool     da_doc_lan_dau = false;
            static bool     cho_xoa = false;         // đã ghi PHIM_TRONG, chờ đọc thấy nó
            static uint16_t phim_cu = PHIM_TRONG;
            if (!da_doc_lan_dau) {                   // lần đầu: giá trị còn sót -> chỉ xóa, không tính là bấm
                da_doc_lan_dau = true;
                if (phim != PHIM_TRONG) {
                    gui_khung(0x10, DC_PHIM_MAT_KHAU, PHIM_TRONG);
                    cho_xoa = true;
                    phim_cu = phim;
                }
            } else if (phim == PHIM_TRONG) {
                cho_xoa = false;
            } else if (!cho_xoa || phim != phim_cu) {    // phím mới
                hmi_su_kien_t e = { .dia_chi = DC_PHIM_MAT_KHAU, .gia_tri = phim, .nguon = SK_NGUON_DOC };
                xQueueSend(hang_doi_su_kien, &e, 0);
                gui_khung(0x10, DC_PHIM_MAT_KHAU, PHIM_TRONG);
                cho_xoa = true;
                phim_cu = phim;
            } else {
                gui_khung(0x10, DC_PHIM_MAT_KHAU, PHIM_TRONG);   // lệnh xóa trước bị mất -> ghi lại
            }

            // Sismo: giá trị đổi so với lần đọc trước = đang bấm / đang giữ
            static int32_t sismo_cu = -1;
            if (sismo_cu >= 0 && sismo != (uint16_t)sismo_cu) {
                hmi_su_kien_t e = { .dia_chi = DC_NUT_SISMO, .gia_tri = sismo, .nguon = SK_NGUON_DOC };
                xQueueSend(hang_doi_su_kien, &e, 0);
            }
            sismo_cu = sismo;
            return;
        }
        xSemaphoreTake(khoa, portMAX_DELAY);
        uint16_t cd_cu = dl.bien[0];
        bool co_cd = false;
        for (int i = 0; i < so_word; i++) {
            uint16_t v = (p[5 + 2 * i] << 8) | p[6 + 2 * i];
            uint16_t a = dia_chi + i;
            if (a == 0x7000) dl.trang = v;
            else if (a >= 0x1000 && a < 0x1000 + HMI_SO_BIEN) {
                dl.bien[a - 0x1000] = v;
                dl.co_bien = true;
                if (a == 0x1000) co_cd = true;
            }
        }
        dl.lan_nhan_cuoi_ms = ms_now();
        uint16_t cd_moi = dl.bien[0];
        xSemaphoreGive(khoa);
        if (co_cd) bao_doi_cuong_do(cd_cu, cd_moi, "doc dinh ky");
    }
    // Touch Returned Message: 41 | addr(2) | giá trị(2)
    else if (p[0] == 0x41 && n == 7) {
        uint16_t dia_chi = (p[1] << 8) | p[2];
        uint16_t v = (p[3] << 8) | p[4];
        if (dia_chi >= 0x1000 && dia_chi < 0x1000 + HMI_SO_BIEN) {
            xSemaphoreTake(khoa, portMAX_DELAY);
            uint16_t cd_cu = dl.bien[0];
            dl.bien[dia_chi - 0x1000] = v;
            xSemaphoreGive(khoa);
            if (dia_chi == 0x1000) {
                xQueueSend(hang_doi_cham, &v, 0);
                bao_doi_cuong_do(cd_cu, v, "man hinh tu bao (0x41)");
            }
        } else if (dia_chi == DC_NUT_SISMO || dia_chi == DC_NUT_CAI_DAT) {
            // (phím mật khẩu 0x1011 KHÔNG lấy từ 0x41 nữa - đã đọc trực tiếp ở trên, lấy cả 2 sẽ bị đếm đôi)
            hmi_su_kien_t e = { .dia_chi = dia_chi, .gia_tri = v, .nguon = SK_NGUON_BAO };
            xQueueSend(hang_doi_su_kien, &e, 0);
        }
    }
}

typedef enum { CHO_5A, CHO_A5, CHO_LEN, DOC } buoc_t;
static buoc_t   st = CHO_5A;
static uint8_t  buf[260];
static uint16_t len_khung = 0, idx = 0;

static void nhan_byte(uint8_t c)
{
    switch (st) {
    case CHO_5A:  if (c == 0x5A) st = CHO_A5; break;
    case CHO_A5:  st = (c == 0xA5) ? CHO_LEN : (c == 0x5A ? CHO_A5 : CHO_5A); break;
    case CHO_LEN: len_khung = c; idx = 0; st = (len_khung > 0) ? DOC : CHO_5A; break;
    case DOC:
        buf[idx++] = c;
        if (idx >= len_khung) { xu_ly_khung(buf, len_khung); st = CHO_5A; }
        break;
    }
}

// ---------------- Task nhận: ngủ cho tới khi có byte về ----------------
static void task_hmi_nhan(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    uint8_t rx[128];
    while (1) {
        // Chờ byte ĐẦU TIÊN (tối đa 100 ms), rồi lấy hết phần đã có sẵn, không chờ thêm.
        // (Không gọi uart_read_bytes(..., 128, ...): hàm đó chờ đủ 128 byte, mà màn hình trả lời
        //  liên tục nên ~300 ms mới đủ -> mọi dữ liệu màn hình bị trễ tới 300 ms.)
        int n = uart_read_bytes(HMI_UART, rx, 1, pdMS_TO_TICKS(100));
        if (n > 0) {
            size_t co_san = 0;
            uart_get_buffered_data_len(HMI_UART, &co_san);
            if (co_san > sizeof(rx) - 1) co_san = sizeof(rx) - 1;
            if (co_san) {
                int m = uart_read_bytes(HMI_UART, rx + 1, co_san, 0);
                if (m > 0) n += m;
            }
        }
        for (int i = 0; i < n; i++) nhan_byte(rx[i]);
        esp_task_wdt_reset();               // báo watchdog "task này còn sống"
    }
}

// ---------------- Task hỏi ----------------
// Mỗi nửa chu kỳ (50 ms) gửi 1 câu hỏi, xen kẽ:
//   - phím mật khẩu 0x1011 + nút Sismo 0x1012 (mỗi 100 ms)
//   - biến 0x1000..0x1009 / trang 0x7000 như cũ
// Hỏi thưa vừa đủ: đồng hồ cường độ do màn hình tự vẽ, việc bấm +/- màn hình tự báo (0x41).
static void task_hmi_hoi(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    TickType_t moc = xTaskGetTickCount();
    const int so_lan_hoi_trang = HMI_CHU_KY_TRANG_MS / HMI_CHU_KY_BIEN_MS;
    int dem = 0;
    bool luot_sismo = false;
    while (1) {
        vTaskDelayUntil(&moc, pdMS_TO_TICKS(HMI_CHU_KY_BIEN_MS / 2));   // chu kỳ đều, không trôi
        luot_sismo = !luot_sismo;
        if (luot_sismo) {
            gui_khung(0x03, DC_PHIM_MAT_KHAU, 2);    // 0x1011, 0x1012
        } else if (++dem >= so_lan_hoi_trang) {
            dem = 0;
            gui_khung(0x03, 0x7000, 1);
        } else {
            gui_khung(0x03, 0x1000, HMI_SO_BIEN);    // 0x1000..0x1009
        }
        esp_task_wdt_reset();

#if THU_TREO_TASK
        if (ms_now() > 20000) {
            nhat_ky_gui("!! THU: task hmi_hoi bi treo (watchdog se khoi dong lai sau %d s)", WDT_TIMEOUT_MS / 1000);
            while (1) vTaskDelay(pdMS_TO_TICKS(1000));   // treo: không còn báo watchdog
        }
#endif
    }
}

void hmi_khoi_dong(void)
{
    khoa = xSemaphoreCreateMutex();
    hang_doi_cham = xQueueCreate(16, sizeof(uint16_t));
    hang_doi_su_kien = xQueueCreate(16, sizeof(hmi_su_kien_t));

    const uart_config_t cfg = {
        .baud_rate  = HMI_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(HMI_UART, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HMI_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(HMI_UART, HMI_TX_PIN, HMI_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreatePinnedToCore(task_hmi_nhan, "hmi_nhan", STACK_HMI, NULL, UU_TIEN_HMI_NHAN, NULL, CORE_UNG_DUNG);
    xTaskCreatePinnedToCore(task_hmi_hoi,  "hmi_hoi",  STACK_HMI, NULL, UU_TIEN_HMI_HOI,  NULL, CORE_UNG_DUNG);
}
