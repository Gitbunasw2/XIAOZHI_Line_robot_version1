# RB Do Line PID (ESP32-S3)

Board rieng cho robot do line. Chan co dinh trong `config.h`.
Menuconfig: Xiaozhi Assistant -> Board Type -> **RB Do Line PID**. Ban co Bluetooth AT-09: **RB Do Line PID + AT09** (`robot-line-pid-at09`).

| Thiet bi | Chan |
|---|---|
| Man ST7789 2.8" 320x240 | SDA 10, SCL 9, DC 8, CS 14, RES 18, BL 13 |
| Mic INMP441 | WS 4, SCK 5, DIN 6 |
| Loa MAX98357 | DOUT 7, BCLK 17, LRCK 16 |
| Nut | BOOT 0, Vol+ 40, Vol- 39 |
| TB6612 banh trai (Motor A) | AIN1 21, AIN2 47, PWMA 12 |
| TB6612 banh phai (Motor B) | BIN1 42, BIN2 41, PWMB 11 |
| TB6612 | STBY noi VCC, VCC 3.3V, VM nguon motor, GND chung |
| Cam bien line S0..S5 (S0 = trai nhat) | 1, 19, 2, 15, 20, 3 |

## Cam bien
- S0 (GPIO1), S2 (GPIO2), S5 (GPIO3) la ADC1, doc analog on dinh khi bat Wi-Fi.
- S1 (GPIO19), S3 (GPIO15), S4 (GPIO20) la ADC2. Wi-Fi dang chiem ADC2 thi giu gia tri doc truoc.

## Dung
1. Dat robot len vach, noi "hieu chinh cam bien" (hoac GIU LAU Vol-). Robot xoay tai cho 5 giay
   va luu ket qua (giu sau khi tat nguon). Sau do noi "do line" thi moi chay.
2. Noi "do line" (hoac GIU LAU Vol+). Noi "dung" / "dung do line" hoac giu Vol+ de dung.
3. Mat vach qua 2 giay thi dung va thoat do line. Do line du 10 giay thi dung va noi
   "toi met qua khong muon chay nua, sap het pin roi".

Lenh khac: "do line nhanh len / cham lai", "chinh PID kp 0.05 kd 0.2", "doc cam bien line",
tien / lui / re trai / re phai / dung.

## Chinh
- Banh quay nguoc: doi `MOTOR_INVERT_LEFT` / `MOTOR_INVERT_RIGHT`.
- Doi banh trai sang phai: `MOTOR_SWAP_LR true`, hoac tick "Dao trai qua phai" tren trang Wi-Fi roi luu. Ap dung ca do line.
- Vach trang tren nen den: `LINE_WHITE_ON_BLACK true`.
- PID mac dinh: Kp 0.04, Ki 0, Kd 0.1, toc do do line 50%. Lenh tien/lui/re mac dinh 20%. Lac nhieu -> giam Kp hoac tang Kd. Cua khong kip -> tang Kp.
- Doi driver: `MOTOR_DRIVER` (L298n, L293d, Drv8833, L9110s, Mx1508, Bts7960, Generic4).

## Luu y
- GPIO19 la cam bien S1, dong thoi la USB D-: mat cong USB native, nap / xem log qua cong UART.
- GPIO43/44 (TX/RX) khong dung tren board nay.
