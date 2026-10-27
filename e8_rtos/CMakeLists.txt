# E8+ (ESP-IDF). Các module nằm trong components/; các trang nằm sâu 1 cấp (components/pages/...)
# nên phải khai báo thêm trong EXTRA_COMPONENT_DIRS cho ESP-IDF tìm thấy (giống mẫu SISMO_E8_new).
# PROJECT_VER: phiên bản firmware, hiện trên tab Upload của web + log khởi động; TĂNG mỗi lần phát
# hành bản mới, và phải đặt TRƯỚC dòng include(...project.cmake).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/components/pages/page_session
    ${CMAKE_CURRENT_LIST_DIR}/components/pages/page_setting
    ${CMAKE_CURRENT_LIST_DIR}/components/pages/page_program)

set(PROJECT_VER "1.14.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(e8_rtos)
