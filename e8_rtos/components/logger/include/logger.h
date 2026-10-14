/*
 * logger.h - Task in log ra Monitor (ưu tiên thấp nhất)
 *
 * Các task khác KHÔNG printf trực tiếp mà gửi chuỗi vào hàng đợi bằng log_send().
 * Hàm này không bao giờ chờ: hàng đợi đầy thì bỏ dòng log, task điều khiển không bị chậm.
 *
 * Module khác có thể đăng ký "hook" (logger_add_hook, tối đa LOGGER_MAX_HOOKS): hàm được gọi
 * khoảng 100 ms/lần trong task logger để in báo cáo định kỳ (trang hiện tại, tiến trình buổi tập,
 * chẩn đoán...). Hook chạy trong task logger nên được printf trực tiếp. Đăng ký lúc khởi động.
 */
#pragma once

#define LOGGER_MAX_HOOKS          6

void logger_start(void);
void log_send(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logger_add_hook(void (*hook)(void));
