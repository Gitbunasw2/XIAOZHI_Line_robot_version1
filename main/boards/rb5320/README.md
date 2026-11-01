# RB HC04 + man phu (ESP32-S3)

Board rieng cho robot RB5320XZ78AI (thu muc `rb5320`). Menuconfig: **RB HC04 + man phu**.
Chan co dinh trong `config.h`. Ban co Bluetooth AT-09 thay man phu: board **RB AT09 + HC04** (`rb-at09-hc04`).

| Thiet bi | Chan |
|---|---|
| Man ST7789 2.8" 320x240 | SDA 10, SCL 9, DC 8, CS 14, RES 18, BL 13 |
| Mic INMP441 | WS 4, SCK 5, DIN 6 |
| Loa MAX98357 | DOUT 7, BCLK 15, LRCK 16 |
| Nut | BOOT 0, Vol+ 40, Vol- 39 |
| Motor | Trai IN1 11 / IN2 12, Phai IN3 21 / IN4 47 |
| HC-SR04 | TRIG 1, ECHO 2 |
| LED | LED1 17, LED2 48 |
| The nho microSD (SPI2) | MISO 42, CLK 41, MOSI 20, CS 19 |
| OLED phu SSD1306 0.96" (I2C 0x3C) | SDA 3, SCL 46 |
| Bao sac | 38 |

Khong con hong ngoai (41/42 da chuyen cho the nho).

## The nho + nhac
- The FAT32. File `.mp3` o thu muc goc hoac `music/`, ten KHONG DAU.
  Muon ten co dau: menuconfig -> Component config -> FAT Filesystem support -> API character encoding -> UTF-8.
- Noi: "mo nhac", "mo bai ...", "mo bai so 3", "bai tiep", "bai truoc", "tiep tuc nghe nhac", "tat nhac", "co bai nao".
- Nhac phat khi robot ranh. Bam BOOT hoac goi robot thi nhac dung.

## OLED phu
Gio, Wi-Fi, pin, trang thai; 2 mat robot theo cam xuc (chop mat); khoang cach HC-SR04; chu AI dang noi (bo dau).
Hinh bi nguoc thi doi `OLED2_FLIP` thanh `true`.

## Luu y
- GPIO19/20 la chan USB D-/D+: mat cong USB native. Nap / xem log qua cong UART (CH340, GPIO43/44).
- GPIO46 la chan strapping: dien tro keo len cua OLED lam GPIO46 = 1. Neu nap firmware bao loi vao che do nap, rut OLED (hoac day VCC OLED) roi nap lai.
