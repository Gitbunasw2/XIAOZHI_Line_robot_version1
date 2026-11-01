// The nho microSD qua SPI (bus rieng SPI2_HOST), gan vao /sdcard (FAT32).
#ifndef _MULTI_SD_CARD_H_
#define _MULTI_SD_CARD_H_

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#define SD_MOUNT_POINT "/sdcard"

// Tra ve true neu gan the thanh cong. Chan -1 = khong dung.
inline bool MountSdCard(int miso, int clk, int mosi, int cs, spi_host_device_t host = SPI2_HOST) {
    static const char* TAG = "SdCard";
    if (miso < 0 || clk < 0 || mosi < 0 || cs < 0) return false;

    spi_bus_config_t bus = {};
    bus.mosi_io_num = mosi;
    bus.miso_io_num = miso;
    bus.sclk_io_num = clk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    esp_err_t err = spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus the nho loi: %s", esp_err_to_name(err));
        return false;
    }

    sdmmc_host_t sd_host = SDSPI_HOST_DEFAULT();
    sd_host.slot = host;
    sd_host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20 MHz

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = host;
    slot.gpio_cs = (gpio_num_t)cs;

    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;  // KHONG tu format, tranh mat du lieu
    mount.max_files = 4;
    mount.allocation_unit_size = 16 * 1024;

    sdmmc_card_t* card = nullptr;
    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &sd_host, &slot, &mount, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong gan duoc the nho (%s). The phai la FAT32, cam chac, dung chan MISO/CLK/MOSI/CS",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "The nho %s: %llu MB tai " SD_MOUNT_POINT, card->cid.name,
             (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024));
    return true;
}

#endif  // _MULTI_SD_CARD_H_
