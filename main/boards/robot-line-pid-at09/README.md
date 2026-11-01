# RB Do Line PID + AT09 (ESP32-S3)

Giong board **RB Do Line PID** (thu muc `robot-line-pid`, dung chung code), them module Bluetooth BLE
**AT-09 / HM-10** de dieu khien bang app **Dabble**. Menuconfig: **RB Do Line PID + AT09**.

| AT-09 | ESP32-S3 |
|---|---|
| RXD | GPIO46 (ESP TX) |
| TXD | GPIO38 (ESP RX) |
| VCC / GND | 3.3-5V / GND |

| Nut Dabble | Chuc nang |
|---|---|
| START | Bat do line (giong noi "do line") |
| SELECT | Tat do line, ve lai tay |
| Mui ten / joystick | Lai (dang do line ma bam mui ten -> tu ve lai tay) |
| X | Dung |
| TRON | Hieu chinh cam bien line (dat robot len vach) |
| TAM GIAC | Ca giut |

Cac chan khac va cach chinh PID: xem `config.h` va README cua `robot-line-pid`.
