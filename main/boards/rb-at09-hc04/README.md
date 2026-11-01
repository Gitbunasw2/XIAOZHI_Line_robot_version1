# RB AT09 + HC04 (ESP32-S3)

Giong board **RB HC04 + man phu** (thu muc `rb5320`, dung chung code) nhung BO man OLED phu,
2 chan 3 / 46 dung cho module Bluetooth BLE **AT-09 / HM-10** de dieu khien bang app **Dabble**.
Menuconfig: **RB AT09 + HC04**. Chan khac xem `config.h` (giong RB HC04).

| AT-09 | ESP32-S3 |
|---|---|
| RXD | GPIO46 (ESP TX) |
| TXD | GPIO3 (ESP RX) |
| VCC | 3.3V hoac 5V |
| GND | GND |

Khong dao 2 chan: TXD cua AT-09 luon o muc 1, noi vao GPIO46 se lam ESP32-S3 khong vao duoc che do nap.

## App Dabble (Android / iOS)
1. Mo Dabble -> bieu tuong ket noi -> chon module (ten mac dinh thuong la "BT05", "HMSoft" hoac "AT-09").
2. Chon **Gamepad** (che do nut hoac joystick deu duoc).

| Nut | Chuc nang |
|---|---|
| START | Bat tranh vat can (giong noi "tranh vat can") |
| SELECT | Tat tranh vat can, ve lai tay |
| Mui ten / joystick | Lai (dang tranh vat can ma bam mui ten -> tu ve lai tay) |
| X | Dung |
| VUONG | Bat / tat den |
| TRON | Lac lu |
| TAM GIAC | Ca giut |

App ngat ket noi khi dang lai -> robot tu dung. Lenh giong noi van dung binh thuong.
