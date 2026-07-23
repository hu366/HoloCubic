/**
 * @file    hal_sd.c
 * @brief   Task 32: SD 卡 HAL 驱动 —— SPI → FatFS 挂载与目录遍历
 *
 * 硬件：ESP32-S3 SPI3 → MicroSD 卡槽
 * 引脚：SCLK=4, MOSI=7, MISO=3, CS=13（app_config.h）
 *
 * 实现策略：
 *   - SPI 总线由本模块独占管理（SPI3_HOST），不与其他外设共享。
 *   - mount 时初始化 SPI 总线 + 检测 SD 卡 + 挂载 FatFS。
 *   - unmount 时卸载 FatFS + 释放 sdmmc 句柄 + 释放 SPI 总线。
 *   - list_dir 使用 POSIX opendir/readdir，结果分为目录/文件两类排序返回。
 *
 * 依赖组件（需在 CMakeLists.txt REQUIRES 中添加）：
 *   esp_driver_sdspi  → driver/sdspi_host.h
 *   sdmmc             → sdmmc_cmd.h
 *   fatfs             → esp_vfs_fat.h （若已有 FatFS 相关依赖可能已包含）
 *   esp_driver_spi    → driver/spi_common.h（已有）
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#include "hal_sd.h"
#include "app_config.h"

static const char *TAG = "hal_sd";

/* ================================================================
 *  内部状态
 * ================================================================ */

static bool          s_mounted  = false; /* FatFS 是否已挂载 */
static bool          s_bus_init = false; /* SPI 总线是否已初始化 */
static bool          s_gpio_isr = false; /* GPIO ISR 服务是否已安装 */
static sdmmc_card_t *s_card     = NULL;  /* SD 卡句柄（unmount 时释放） */

/* ================================================================
 *  公开接口
 * ================================================================ */

bool hal_sd_mount(void) {
    /* 幂等：已挂载直接返回 */
    if (s_mounted) {
        ESP_LOGW(TAG, "Already mounted");
        return true;
    }

    esp_err_t ret;

    /* ---------- 1. 预初始化 SPI 总线（自定义引脚） ---------- *
     * 先占住总线总线，让后续 sdspi_host_init_device 检测到总线已占用
     * 并复用我们的自定义引脚配置。                                          */
    if (!s_bus_init) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num     = SD_PIN_MOSI,
            .miso_io_num     = SD_PIN_MISO,
            .sclk_io_num     = SD_PIN_SCLK,
            .quadwp_io_num   = -1,
            .quadhd_io_num   = -1,
            .max_transfer_sz = 65536,   /* 64KB: 减少 DMA 事务数，提升连续读取速度 */
        };
        ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
            return false;
        }
        s_bus_init = true;
        ESP_LOGI(TAG, "SPI bus initialized (SPI3_HOST)");
    }

    /* ---------- 2. 安装 GPIO ISR 服务 ---------- */
    if (!s_gpio_isr) {
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
            return false;
        }
        s_gpio_isr = true;
        ESP_LOGI(TAG, "GPIO ISR service installed");
    }

    /* ---------- 3. SDMMC 主机配置 ---------- *
     * 保持 SDSPI_HOST_DEFAULT() 的 .init = sdspi_host_init。
     * esp_vfs_fat_sdspi_mount 内部会调用 sdmmc_host_init → host->init()
     * → sdspi_host_init → sdspi_host_init_device，后者检测到 SPI 总线
     * 已在步骤 1 中被占用，将复用现有总线配置（即我们的自定义引脚）。      */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    /* ---------- 4. SDSPI 槽位配置 ---------- */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs  = SD_PIN_CS;
    slot_config.host_id  = host.slot;

    /* ---------- 5. FatFS 挂载配置 ---------- */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files             = SD_MAX_FILES_OPEN,
        .allocation_unit_size  = 16 * 1024,
    };

    /* ---------- 6. 挂载 ---------- */
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host,
                                   &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        /* 挂载失败不释放 SPI 总线，调用者可重试 */
        s_card = NULL;
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);

    /* 打印卡信息 */
    if (s_card) {
        ESP_LOGI(TAG, "Card: %s, %dMB, sector_size=%d",
                 s_card->cid.name,
                 (int)((uint64_t)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024)),
                 s_card->csd.sector_size);
    }

    return true;
}

void hal_sd_unmount(void) {
    if (!s_mounted) {
        /* 未挂载时释放已分配的资源（防御性清理） */
        if (s_bus_init) {
            spi_bus_free(SD_SPI_HOST);
            s_bus_init = false;
        }
        return;
    }

    /* 卸载 FatFS 并释放 SD 卡 */
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unmount warning: %s", esp_err_to_name(ret));
    }
    s_card     = NULL;
    s_mounted  = false;

    /* esp_vfs_fat_sdcard_unmount 内部调了 sdspi_host_deinit，
       但预初始化的 SPI 总线可能未被完全释放，手动补充释放。 */
    ret = spi_bus_free(SD_SPI_HOST);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI bus free note: %s", esp_err_to_name(ret));
    }
    s_bus_init = false;
    /* s_gpio_isr 保持 true —— ISR 服务在整个应用生命周期只安装一次 */

    ESP_LOGI(TAG, "SD card unmounted");
}

/* ================================================================
 *  目录列表
 * ================================================================ */

/**
 * @brief 比较函数：排序用 —— 目录在前、文件在后，各自按名称字典序
 */
static int sd_entry_cmp(const void *a, const void *b) {
    const char *name_a = *(const char **)a;
    const char *name_b = *(const char **)b;

    /* 判断是否为目录（以 '/' 结尾） */
    bool is_dir_a = (name_a[strlen(name_a) - 1] == '/');
    bool is_dir_b = (name_b[strlen(name_b) - 1] == '/');

    if (is_dir_a && !is_dir_b) return -1;
    if (!is_dir_a && is_dir_b) return 1;

    return strcasecmp(name_a, name_b);
}

bool hal_sd_list_dir(const char *path, char ***names, size_t *count) {
    /* 参数校验 */
    if (names == NULL || count == NULL) {
        ESP_LOGE(TAG, "list_dir: names or count is NULL");
        return false;
    }
    *names = NULL;
    *count = 0;

    if (path == NULL) {
        ESP_LOGE(TAG, "list_dir: path is NULL");
        return false;
    }

    if (!s_mounted) {
        ESP_LOGE(TAG, "list_dir: SD not mounted");
        return false;
    }

    /* 打开目录 */
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "list_dir: opendir(%s) failed", path);
        return false;
    }

    /* ---- 第一遍：统计条目数（跳过 . 和 ..） ---- */
    struct dirent *entry;
    size_t n = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        n++;
    }

    if (n == 0) {
        closedir(dir);
        *names = NULL;
        *count = 0;
        return true;  /* 空目录不算失败 */
    }

    /* ---- 第二遍：分配内存并填充名称 ---- */
    char **list = (char **)calloc(n, sizeof(char *));
    if (list == NULL) {
        ESP_LOGE(TAG, "list_dir: calloc(%zu) failed", n);
        closedir(dir);
        return false;
    }

    rewinddir(dir);
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < n) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* 判断是否为目录：通过 stat 或 d_type */
        bool is_dir = false;
#ifdef DT_DIR
        /* POSIX dirent 的 d_type 字段（FatFS 可能不支持，降级到 stat） */
        if (entry->d_type == DT_DIR) {
            is_dir = true;
        }
#endif
        /* 降级：构造完整路径用 stat 判断 */
        if (!is_dir) {
            char full_path[512];
            int written = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            if (written > 0 && (size_t)written < sizeof(full_path)) {
                struct stat st;
                if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    is_dir = true;
                }
            }
        }

        /* 目录名称末尾添加 '/' */
        size_t name_len = strlen(entry->d_name);
        size_t alloc_len = name_len + (is_dir ? 2 : 1);  /* +2 for '/' + '\0' */
        list[idx] = (char *)malloc(alloc_len);
        if (list[idx] == NULL) {
            ESP_LOGE(TAG, "list_dir: malloc(%zu) failed", alloc_len);
            /* 释放已分配的内存 */
            for (size_t j = 0; j < idx; j++) {
                free(list[j]);
            }
            free(list);
            closedir(dir);
            return false;
        }
        strcpy(list[idx], entry->d_name);
        if (is_dir) {
            list[idx][name_len] = '/';
            list[idx][name_len + 1] = '\0';
        }
        idx++;
    }
    closedir(dir);

    /* ---- 排序：目录在前，文件在后，各自字典序 ---- */
    qsort(list, n, sizeof(char *), sd_entry_cmp);

    *names = list;
    *count = n;
    return true;
}

/* ================================================================
 *  内存释放
 * ================================================================ */

void hal_sd_free_dir_list(char **names, size_t count) {
    if (names == NULL) return;

    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}
