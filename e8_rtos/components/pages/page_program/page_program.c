/*
 * page_program.c - Programmes: chọn mức độ, trang buổi tập 0006 / 0014, trang bài tập con 0013, chạy bài
 * (xem page_program.h; số liệu 22 bài trong program_data.c)
 *
 * Số bài 0x1007: 0..4 Tonification, 5..9 Remodelage, 10 Anti-cellulite, 11..12 Gainage,
 * 13..15 Relaxation, 16..21 Proprioception, 22..27 Perso 1..6 (trang 0007 ghi 0x1007 rồi tự sang 0014).
 * Tên bài số n = bài * 3 + mức + 1 (16 bài đầu) hoặc 48 + (bài - 16) + 1 (Proprioception / Perso), mỗi tên 6
 * ngôn ngữ: n = 1..48 ở Icon 0x1051 (526x40, x = 55), n = 49..60 ở Icon 0x105B (190x40, x = 0) -> 2 Icon đè
 * nhau nên lúc vào bài ghi cả 2 = 0 (ẩn), lần đọc parameterAddr đầu tiên mới ghi Icon cần hiện =
 * LANG_ICON(n - 1 hoặc n - 49, ngôn ngữ). Proprioception / Perso chỉ có 1 mức, nút < về 0025 / 0007.
 * Nút < ở trang mức độ 0005 (0x1059 = 6): về trang chọn bài của nhóm (0004 / 0021 / 0022 / 0023 / 0024).
 *
 * Vào trang buổi tập: ghi hình bài tập con đầu, tên bài, trục, cường độ cột đầu, thời gian; sau
 * PARAM_DELAY_MS đọc 10 word parameterAddr của Progress Bar (0x2020 / 0x2030; đọc lại tối đa 4 lần, hỏng
 * thì dùng bản đọc lần trước), đổi byte 7-8 (barIcon = 0860 + n xanh) và 18-19 (background = 0800 + n
 * xám), ghi lại, ghi 0x105A = 0.
 * Màn hình chỉ vẽ lại widget có giá trị đổi, theo thứ tự widget: widget nằm dưới đổi mà widget nằm trên
 * không đổi thì widget trên bị che. Vì vậy hình minh họa được cắt thành 4 phần không đè lên đồng hồ (UI),
 * còn trục biểu đồ (0x1052) đè lên vòng cường độ: lúc vào bài ghi vòng cường độ = 0 (ẩn), lần đọc
 * parameterAddr đầu tiên (sau PARAM_DELAY_MS) mới ghi mức thật. Mỗi lần ghi hình minh họa (0x1050) báo luôn
 * page_session_set_illus() để thông báo lỗi (nằm đè lên hình) biết phần hình bên dưới nó.
 *
 * Chạy bài (trạng thái IDLE -> COUNTDOWN -> RUN <-> PAUSE):
 *   ▶ lúc IDLE hoặc tạm dừng: bíp BEEP_COUNT lần cách BEEP_INTERVAL_MS rồi chạy / chạy tiếp (lúc bíp biểu
 *   đồ giữ nguyên); ▶ lúc chạy: tạm dừng; ▶ lúc đang bíp: hủy (về IDLE nếu chưa chạy, về tạm dừng nếu
 *   đang chạy dở). ■ hoặc hết bài: về IDLE, vẽ lại trang như lúc mới vào.
 *   Cột thứ i = thời gian đã chạy / (1 s hoặc 5 s); cột đang chạy chớp BLINK_MS, cột đã qua sáng hẳn
 *   (giá trị Progress Bar = số cột đã qua + 1 khi đang sáng). Vòng cường độ = mức cột hiện tại.
 *   Sang bài tập con mới: bíp 1 lần + đổi hình. Thời gian hiện = còn lại.
 *   Xung (bật / tắt bất cứ lúc nào, ■ tắt): khi chạy, mức = mức cao của đoạn hiện tại + dao động tam giác
 *   ±PULSE_AMPLITUDE (mỗi PULSE_STEP_MS đổi 1 mức); các cột đã / đang sáng đổi sang ảnh cột đều
 *   0920 + mức - 1, cột xám chưa tới giữ nguyên.
 *   Nút X (chỉ 0006) chỉ dùng được lúc IDLE (icon X xám khi đang chạy / tạm dừng). Nút < lúc đang chạy:
 *   dừng bài rồi mới chuyển trang. Rời trang buổi tập: về IDLE.
 *   Động cơ: báo task session "đang chạy + mức" (lúc bíp đếm ngược mức 0); task session lo an toàn
 *   (thẻ, mạng, CM8, liên lạc màn hình) và yêu cầu tạm dừng khi cần.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "app_config.h"
#include "logger.h"
#include "page_session.h"
#include "page_program.h"
#include "program_data.h"
#include "grid_data.h"
#include "device_settings.h"

#define FIRST_PROPRIO             16
#define FIRST_PERSO               22
#define TITLE_SHORT_FIRST         49
#define TITLE_NONE                0
#define AXIS_SHORT_MAX_S          120
#define LOOP_MS                   50
#define PARAM_WORDS               10
#define PARAM_LEN                 0x13
#define PARAM_BAR_ICON_BYTE       7
#define PARAM_BACKGROUND_BYTE     18
#define PARAM_DELAY_MS            150
#define PARAM_RETRY_MS            250
#define PARAM_TRIES               4
#define BEEP_COUNT                5
#define BEEP_INTERVAL_MS          750
#define BLINK_MS                  250
#define PULSE_AMPLITUDE           3
#define PULSE_STEP_MS             300
#define LEVEL_HIDDEN              0

typedef enum { ST_IDLE, ST_COUNTDOWN, ST_RUN, ST_PAUSE } run_state_t;

static const char *LEVEL_NAMES[PROGRAM_LEVELS] = { "Debutant", "Initie", "Confirme" };
static const char *STATE_NAMES[] = { "CHO", "DEM NGUOC", "CHAY", "TAM DUNG" };

static QueueHandle_t event_queue;
static int program = -1;
static int level = 0;
static int grid_page = 0;

static uint16_t param[PARAM_WORDS];
static uint16_t param_cache[2][PARAM_WORDS];
static bool param_cached[2];
static bool param_ready = false;
static uint16_t param_addr = ADDR_PROG_PARAM_X;
static int chart_tries = 0;
static int64_t chart_at = 0;

static uint8_t bar_level[PROGRAM_SLOTS];
static uint8_t bar_high[PROGRAM_SLOTS];
static int     bar_count = 0;
static int     bar_ms = 5000;
static int64_t total_ms = 0;

static run_state_t state = ST_IDLE;
static int64_t elapsed_ms = 0;
static int64_t last_tick_ms = 0;
static int     beep_left = 0;
static int64_t next_beep_ms = 0;
static bool    pulse = false;
static int64_t pulse_start_ms = 0;
static int64_t blink_ms = 0;
static bool    blink_on = true;

static int drawn_value = -1;
static int drawn_comb = -1;
static int drawn_level = -1;
static int drawn_seconds = -1;
static int drawn_sub = -1;
static int logged_bar = -1;
static int title_number = 1;

static portMUX_TYPE info_lock = portMUX_INITIALIZER_UNLOCKED;
static page_program_info_t shared_info = { PROGRAM_IDLE, -1, 0, 0, 0, NULL };

static int64_t ms_now(void) { return esp_timer_get_time() / 1000; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int prog_level(void) { return program >= FIRST_PROPRIO ? 0 : level; }
static const program_info_t *info(void) { return &PROGRAM_INFO[program]; }
static int session_page(int prog) { return PROGRAM_INFO[prog].has_x ? PAGE_PROGRAM_X : PAGE_PROGRAM; }

static void build_bars(void)
{
    const program_profile_t *pr = &info()->profile[prog_level()];
    int seconds = info()->seconds[prog_level()];
    bar_ms = seconds <= AXIS_SHORT_MAX_S ? 1000 : 5000;
    total_ms = (int64_t)seconds * 1000;
    bar_count = 0;
    for (int s = 0; s < pr->nsegs; s++) {
        const program_seg_t *g = &pr->segs[s];
        for (int j = 0; j < g->bars && bar_count < PROGRAM_SLOTS; j++, bar_count++) {
            bar_level[bar_count] = (g->low && (j % 2)) ? g->low : g->high;
            bar_high[bar_count] = g->high;
        }
    }
}

static int current_bar(void)
{
    int i = (int)(elapsed_ms / bar_ms);
    return clamp(i, 0, bar_count - 1);
}

static int current_sub(void)
{
    const program_info_t *p = info();
    if (p->nsubs <= 1 || p->subs[0].seconds == 0) return 0;
    int64_t t = 0;
    for (int k = 0; k < p->nsubs; k++) {
        t += (int64_t)p->subs[k].seconds * 1000;
        if (elapsed_ms < t) return k;
    }
    return p->nsubs - 1;
}

static bool started(void)
{
    return state == ST_RUN || state == ST_PAUSE || (state == ST_COUNTDOWN && elapsed_ms > 0);
}

static int pulse_offset(void)
{
    const int steps = 4 * PULSE_AMPLITUDE;
    int k = (int)(((ms_now() - pulse_start_ms) / PULSE_STEP_MS) % steps);
    if (k <= PULSE_AMPLITUDE)     return k;
    if (k <= 3 * PULSE_AMPLITUDE) return 2 * PULSE_AMPLITUDE - k;
    return k - 4 * PULSE_AMPLITUDE;
}

static int display_level(void)
{
    int i = current_bar();
    if (pulse && started()) return clamp(bar_high[i] + pulse_offset(), PROGRAM_COMB_MIN, COMB_LEVELS);
    return bar_level[i];
}

static int remaining_seconds(void)
{
    int64_t rem = total_ms - elapsed_ms;
    if (rem < 0) rem = 0;
    return (int)((rem + 999) / 1000);
}

static void write_time(int seconds)
{
    uint16_t v[2] = { (uint16_t)(seconds / 60), (uint16_t)(seconds % 60) };
    hmi_write_words(ADDR_PROG_MIN, v, 2);
    drawn_seconds = seconds;
}

static void write_illus(int illus)
{
    page_session_set_illus(illus);
    hmi_write(ADDR_PROG_ILLUS, (uint16_t)illus);
}

static void show_illus(int sub)
{
    write_illus(info()->subs[sub].illus);
    drawn_sub = sub;
}

static uint8_t param_byte(int pos)
{
    uint16_t w = param[pos / 2];
    return (pos % 2 == 0) ? (w >> 8) : (w & 0xFF);
}

static void set_param_byte(int pos, uint8_t b)
{
    uint16_t *w = &param[pos / 2];
    if (pos % 2 == 0) *w = (uint16_t)((*w & 0x00FF) | (b << 8));
    else              *w = (uint16_t)((*w & 0xFF00) | b);
}

static void set_param_icon(int pos, uint16_t icon)
{
    set_param_byte(pos, icon & 0xFF);
    set_param_byte(pos + 1, icon >> 8);
}

static bool param_ok(void)
{
    uint16_t write_addr = (uint16_t)(param_byte(1) | (param_byte(2) << 8));
    return param_byte(0) == PARAM_LEN && write_addr == ADDR_PROG_PROGRESS;
}

static void set_bar_icon(uint16_t icon)
{
    if (!param_ready) return;
    set_param_icon(PARAM_BAR_ICON_BYTE, icon);
    hmi_write_words(param_addr, param, PARAM_WORDS);
}

static int progress_value(void)
{
    if (!started()) return 0;
    int i = (int)(elapsed_ms / bar_ms);
    if (i >= bar_count) return bar_count;
    return i + ((state != ST_RUN || blink_on) ? 1 : 0);
}

static void draw_chart(bool force)
{
    int comb = -1;
    if (pulse && started()) comb = display_level() - 1;
    if (comb != drawn_comb && param_ready) {
        int chart = info()->profile[prog_level()].chart;
        set_bar_icon((uint16_t)(comb >= 0 ? ICON_COMB_BASE + comb : ICON_PROFILE_GREEN_BASE + chart));
        drawn_comb = comb;
        force = true;
    }
    int v = progress_value();
    if (force || v != drawn_value) {
        hmi_write(ADDR_PROG_PROGRESS, (uint16_t)v);
        drawn_value = v;
    }
}

static void draw_live(void)
{
    int lv = display_level();
    if (lv != drawn_level) {
        hmi_write(ADDR_PROG_LEVEL, (uint16_t)lv);
        drawn_level = lv;
    }
    int sub = current_sub();
    if (sub != drawn_sub) show_illus(sub);
    int s = remaining_seconds();
    if (s != drawn_seconds) write_time(s);
    draw_chart(false);
}

static void show_buttons(void)
{
    hmi_write(ADDR_PROG_X_ICON, state == ST_IDLE ? 0 : 1);
    hmi_write(ADDR_PROG_PULSE_ICON, pulse ? 1 : 0);
    hmi_write(ADDR_PROG_PLAY_ICON, (state == ST_RUN || state == ST_COUNTDOWN) ? 1 : 0);
}

static void reset_run(void)
{
    state = ST_IDLE;
    elapsed_ms = 0;
    beep_left = 0;
    pulse = false;
    blink_on = true;
    drawn_level = drawn_seconds = drawn_sub = -1;
    logged_bar = -1;
    page_session_program(false, 0);
}

static void show_program(void)
{
    const program_info_t *p = info();
    int lv = prog_level();
    int seconds = p->seconds[lv];
    int title = program < FIRST_PROPRIO ? program * PROGRAM_LEVELS + lv + 1 : FIRST_PROPRIO * PROGRAM_LEVELS + (program - FIRST_PROPRIO) + 1;
    reset_run();
    build_bars();
    write_illus(p->subs[0].illus);
    drawn_sub = 0;
    title_number = title;
    hmi_write(ADDR_PROG_TITLE, TITLE_NONE);
    hmi_write(ADDR_PROG_TITLE_SHORT, TITLE_NONE);
    hmi_write(ADDR_PROG_AXIS, seconds <= AXIS_SHORT_MAX_S ? 1 : 0);
    hmi_write(ADDR_PROG_LEVEL, LEVEL_HIDDEN);
    drawn_level = LEVEL_HIDDEN;
    show_buttons();
    write_time(remaining_seconds());
    if (program >= FIRST_PROPRIO) log_send("   [PROGRAMME] %s | %d:%02d", p->name, seconds / 60, seconds % 60);
    else log_send("   [PROGRAMME] %s - %s | %d:%02d", p->name, LEVEL_NAMES[lv], seconds / 60, seconds % 60);
}

static void show_title(void)
{
    int lang = device_get_language();
    if (title_number < TITLE_SHORT_FIRST) {
        hmi_write(ADDR_PROG_TITLE_SHORT, TITLE_NONE);
        hmi_write(ADDR_PROG_TITLE, LANG_ICON(title_number - 1, lang));
    } else {
        hmi_write(ADDR_PROG_TITLE, TITLE_NONE);
        hmi_write(ADDR_PROG_TITLE_SHORT, LANG_ICON(title_number - TITLE_SHORT_FIRST, lang));
    }
}

static void schedule_chart(void)
{
    param_ready = false;
    chart_tries = PARAM_TRIES;
    chart_at = ms_now() + PARAM_DELAY_MS;
}

static void apply_chart(int page)
{
    int slot = page == PAGE_PROGRAM_X ? 0 : 1;
    param_addr = page == PAGE_PROGRAM_X ? ADDR_PROG_PARAM_X : ADDR_PROG_PARAM;
    if (chart_tries == PARAM_TRIES) {
        drawn_level = display_level();
        hmi_write(ADDR_PROG_LEVEL, (uint16_t)drawn_level);
        show_title();
    }
    chart_tries--;
    bool got = hmi_read_words(param_addr, PARAM_WORDS, param, 300) && param_ok();
    if (got) {
        for (int i = 0; i < PARAM_WORDS; i++) param_cache[slot][i] = param[i];
        param_cached[slot] = true;
    } else if (chart_tries > 0) {
        chart_at = ms_now() + PARAM_RETRY_MS;
        return;
    } else if (param_cached[slot]) {
        for (int i = 0; i < PARAM_WORDS; i++) param[i] = param_cache[slot][i];
    } else {
        log_send("   [PROGRAMME] KHONG doc duoc parameterAddr 0x%04X cua Progress Bar -> bieu do khong doi", param_addr);
        chart_tries = 0;
        return;
    }
    chart_tries = 0;
    int chart = info()->profile[prog_level()].chart;
    int comb = (pulse && started()) ? display_level() - 1 : -1;
    set_param_icon(PARAM_BAR_ICON_BYTE, (uint16_t)(comb >= 0 ? ICON_COMB_BASE + comb : ICON_PROFILE_GREEN_BASE + chart));
    set_param_icon(PARAM_BACKGROUND_BYTE, (uint16_t)(ICON_PROFILE_GRAY_BASE + chart));
    hmi_write_words(param_addr, param, PARAM_WORDS);
    param_ready = true;
    drawn_comb = comb;
    draw_chart(true);
    log_send("   [PROGRAMME] Bieu do %d (icon %04d xam / %04d xanh)%s", chart + 1, ICON_PROFILE_GRAY_BASE + chart,
             ICON_PROFILE_GREEN_BASE + chart, got ? "" : " - dung parameterAddr doc lan truoc");
}

static void stop_run(const char *why)
{
    bool was = state != ST_IDLE;
    bool had_comb = drawn_comb >= 0;
    reset_run();
    if (had_comb && param_ready) {
        set_bar_icon((uint16_t)(ICON_PROFILE_GREEN_BASE + info()->profile[prog_level()].chart));
        drawn_comb = -1;
    }
    write_illus(info()->subs[0].illus);
    drawn_sub = 0;
    hmi_write(ADDR_PROG_LEVEL, bar_level[0]);
    drawn_level = bar_level[0];
    show_buttons();
    write_time(remaining_seconds());
    draw_chart(true);
    if (was) log_send("   [PROGRAMME] %s -> ve lai trang", why);
}

static void set_state(run_state_t next)
{
    state = next;
    last_tick_ms = ms_now();
    blink_ms = last_tick_ms;
    blink_on = true;
    show_buttons();
    draw_live();
    int s = remaining_seconds();
    log_send("   [PROGRAMME] -> %s (con %d:%02d)", STATE_NAMES[next], s / 60, s % 60);
}

static bool read_program(void)
{
    uint16_t v = 0xFFFF;
    if (!hmi_read_words(ADDR_VAR_BASE + VAR_PROGRAM, 1, &v, 300)) v = hmi_get().vars[VAR_PROGRAM];
    if (v >= PROGRAM_COUNT) {
        log_send("   [PROGRAMME] So bai 0x1007 = %u khong hop le", v);
        return false;
    }
    program = v;
    return true;
}

static void show_grid(void)
{
    const program_info_t *p = info();
    int g = (grid_page && p->grid2 ? p->grid2 : p->grid) - 1;
    if (g < 0 || g >= GRID_COUNT) return;
    uint16_t v[GRID_SLOTS + 1];
    for (int i = 0; i < GRID_SLOTS; i++) v[i] = GRID_TILES[g][i];
    v[GRID_SLOTS] = GRID_ARROW[g];
    hmi_write_words(ADDR_GRID_SLOT, v, GRID_SLOTS + 1);
}

static void on_level(int value, int page)
{
    if (page != PAGE_LEVEL || value < 0 || value >= PROGRAM_LEVELS) return;
    if (!read_program() || program >= FIRST_PROPRIO) return;
    level = value;
    show_program();
    hmi_goto_page(session_page(program));
}

static void start_countdown(void)
{
    bool resume = elapsed_ms > 0;
    state = ST_COUNTDOWN;
    beep_left = BEEP_COUNT;
    next_beep_ms = ms_now();
    show_buttons();
    log_send("   [PROGRAMME] Bip %d lan roi %s", BEEP_COUNT, resume ? "chay tiep" : "bat dau");
}

static void on_play(void)
{
    switch (state) {
    case ST_IDLE:
    case ST_PAUSE:
        start_countdown();
        break;
    case ST_COUNTDOWN:
        if (elapsed_ms > 0) set_state(ST_PAUSE);
        else stop_run("Huy dem nguoc");
        break;
    case ST_RUN:
        set_state(ST_PAUSE);
        break;
    }
}

static void on_pulse(void)
{
    pulse = !pulse;
    pulse_start_ms = ms_now();
    show_buttons();
    draw_live();
    log_send("   [PROGRAMME] Xung %s", pulse ? "BAT" : "TAT");
}

static int group_page(int prog)
{
    if (prog < 5)  return PAGE_TONIFICATION;
    if (prog < 10) return PAGE_REMODELAGE;
    if (prog < 11) return PAGE_ANTICELLULITE;
    if (prog < 13) return PAGE_GAINAGE;
    if (prog < FIRST_PROPRIO) return PAGE_RELAXATION;
    return PAGE_PROGRAMMES;
}

static void on_button(int value, int page)
{
    if (value == PROG_BTN_BACK && page == PAGE_LEVEL) {
        hmi_goto_page(read_program() ? group_page(program) : PAGE_PROGRAMMES);
        return;
    }
    if (program < 0) return;
    bool on_session = page == PAGE_PROGRAM_X || page == PAGE_PROGRAM;
    switch (value) {
    case PROG_BTN_X:
        if (page != PAGE_PROGRAM_X || !info()->has_x) return;
        if (state != ST_IDLE) { log_send("   [PROGRAMME] Dang chay -> khong mo bai tap con"); return; }
        grid_page = 0;
        show_grid();
        hmi_goto_page(PAGE_EXERCISES);
        log_send("   [PROGRAMME] Xem bai tap con (luoi %d)", info()->grid);
        break;
    case PROG_BTN_ARROW:
        if (page != PAGE_EXERCISES || !info()->grid2) return;
        grid_page = !grid_page;
        show_grid();
        break;
    case PROG_BTN_PULSE:
        if (on_session) on_pulse();
        break;
    case PROG_BTN_PLAY:
        if (on_session) on_play();
        break;
    case PROG_BTN_STOP:
        if (on_session) stop_run("Bam stop");
        break;
    case PROG_BTN_BACK:
        if (state != ST_IDLE) {
            reset_run();
            log_send("   [PROGRAMME] Bam < khi dang chay -> dung bai");
        }
        hmi_goto_page(program >= FIRST_PERSO ? PAGE_PERSO : program >= FIRST_PROPRIO ? PAGE_PROPRIO : PAGE_LEVEL);
        break;
    default:
        break;
    }
}

static void tick(void)
{
    int64_t now = ms_now();
    if (page_session_program_take_pause() && (state == ST_RUN || state == ST_COUNTDOWN)) {
        if (state == ST_COUNTDOWN && elapsed_ms == 0) stop_run("An toan / the / mang khong cho chay");
        else set_state(ST_PAUSE);
    }
    if (state == ST_COUNTDOWN) {
        if (beep_left > 0 && now >= next_beep_ms) {
            hmi_write(ADDR_WAV, WAV_BEEP);
            beep_left--;
            next_beep_ms = now + BEEP_INTERVAL_MS;
        } else if (beep_left == 0 && now >= next_beep_ms) {
            set_state(ST_RUN);
        }
    } else if (state == ST_RUN) {
        elapsed_ms += now - last_tick_ms;
        last_tick_ms = now;
        if (elapsed_ms >= total_ms) {
            stop_run("Het bai");
        } else {
            if (now - blink_ms >= BLINK_MS) {
                blink_on = !blink_on;
                blink_ms = now;
            }
            int sub = current_sub();
            if (sub != drawn_sub) {
                hmi_write(ADDR_WAV, WAV_BEEP);
                log_send("   [PROGRAMME] Bai tap con %d / %d", sub + 1, info()->nsubs);
            }
            draw_live();
            int i = current_bar();
            if (i != logged_bar) {
                logged_bar = i;
                int r = remaining_seconds();
                log_send("   [PROGRAMME] Cot %d / %d | muc %d%s | con %d:%02d | toc do CM8 %u", i + 1, bar_count,
                         display_level(), pulse ? " (xung)" : "", r / 60, r % 60, page_session_get_status().speed_sent);
            }
        }
    }
    bool running = state == ST_RUN || state == ST_COUNTDOWN;
    page_session_program(running, state == ST_RUN ? display_level() : 0);
}

static void publish_info(bool on_session)
{
    page_program_info_t i = { PROGRAM_IDLE, -1, 0, 0, 0, NULL };
    if (on_session && program >= 0) {
        i.state = (program_state_t)state;
        i.program = program;
        i.level = prog_level();
        i.total_s = info()->seconds[prog_level()];
        int64_t left = total_ms - elapsed_ms;
        i.remaining_s = left > 0 ? (int)((left + 999) / 1000) : 0;
        i.name = info()->name;
    }
    portENTER_CRITICAL(&info_lock);
    shared_info = i;
    portEXIT_CRITICAL(&info_lock);
}

void page_program_get_info(page_program_info_t *out)
{
    portENTER_CRITICAL(&info_lock);
    *out = shared_info;
    portEXIT_CRITICAL(&info_lock);
}

static void task_program(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    int prev_page = -1;
    while (1) {
        hmi_event_t e;
        if (xQueueReceive(event_queue, &e, pdMS_TO_TICKS(LOOP_MS)) == pdTRUE) {
            int page = hmi_get().page;
            if (e.addr == ADDR_VAR_BASE + VAR_LEVEL) on_level(e.value, page);
            else on_button(e.value, page);
        }
        esp_task_wdt_reset();

        int page = hmi_get().page;
        bool on_session = page == PAGE_PROGRAM_X || page == PAGE_PROGRAM;
        bool came_from_other = prev_page != PAGE_PROGRAM_X && prev_page != PAGE_PROGRAM &&
                               prev_page != PAGE_EXERCISES && prev_page != PAGE_LEVEL;
        if (on_session && page != prev_page) {
            if (!came_from_other || read_program()) {
                if (came_from_other) show_program();
                schedule_chart();
            }
        }
        if (!on_session) {
            chart_tries = 0;
            param_ready = false;
            if (state != ST_IDLE) reset_run();
            page_session_program(false, 0);
        } else if (program >= 0) {
            if (chart_tries > 0 && ms_now() >= chart_at) apply_chart(page);
            tick();
        }
        prev_page = page;
        publish_info(on_session);
    }
}

bool page_program_on_event(const hmi_event_t *e)
{
    bool level_btn = e->addr == ADDR_VAR_BASE + VAR_LEVEL;
    if (!level_btn && e->addr != ADDR_PROG_BUTTON) return false;
    if (e->source == EV_SRC_REPORT && event_queue) xQueueSend(event_queue, e, 0);
    return true;
}

void page_program_start(void)
{
    event_queue = xQueueCreate(8, sizeof(hmi_event_t));
    xTaskCreatePinnedToCore(task_program, "program", STACK_PROGRAM, NULL, PRIO_PROGRAM, NULL, APP_CORE);
}
