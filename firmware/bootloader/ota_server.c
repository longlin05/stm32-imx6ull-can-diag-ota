#include "ota_server.h"

#include <string.h>

#include "boot_app.h"
#include "boot_flash_stm32.h"
#include "boot_image.h"
#include "boot_layout.h"

#define OTA_MAX_BLOCK   1024U

typedef struct {
    uint8_t  active;
    uint8_t  target;
    uint8_t  active_flag;
    uint8_t  next_block;
    uint32_t addr;
    uint32_t size;
    uint32_t written;
} ota_ctx_t;

static ota_ctx_t s_ota;

static uint16_t ota_nrc(uint8_t *resp, uint16_t cap,
                        uint8_t sid, uint8_t nrc)
{
    if (cap < 3U) {
        return 0U;
    }
    resp[0] = 0x7FU;
    resp[1] = sid;
    resp[2] = nrc;
    return 3U;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint32_t slot_sector(uint8_t slot)
{
    return (slot == BOOT_SLOT_A) ? 5U : 7U;
}

void OtaServer_Init(uint8_t active_slot)
{
    memset(&s_ota, 0, sizeof s_ota);
    s_ota.active = (active_slot == BOOT_SLOT_B) ? BOOT_SLOT_B : BOOT_SLOT_A;
}

static uint16_t ota_download(const uint8_t *req, uint16_t len,
                             uint8_t *resp, uint16_t cap)
{
    const boot_flash_if_t *flash = boot_flash_stm32();
    uint32_t addr;
    uint32_t size;
    uint8_t  target;

    if (len != 11U) {
        return ota_nrc(resp, cap, req[0], 0x13U);
    }
    if ((req[1] != 0x00U) || (req[2] != 0x44U)) {
        return ota_nrc(resp, cap, req[0], 0x31U);
    }

    addr = be32(&req[3]);
    size = be32(&req[7]);

    if (addr == BOOT_APP_A_BASE) {
        target = BOOT_SLOT_A;
    } else if (addr == BOOT_APP_B_BASE) {
        target = BOOT_SLOT_B;
    } else {
        return ota_nrc(resp, cap, req[0], 0x31U);
    }

    if ((target == s_ota.active) || (size == 0U) ||
        (size > BOOT_APP_SLOT_SIZE)) {
        return ota_nrc(resp, cap, req[0], 0x31U);
    }

    if (flash == 0 || flash->erase_sector == 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }

    /* 两个 128KB sector */
    if ((flash->erase_sector(flash->ctx, slot_sector(target)) != 0) ||
        (flash->erase_sector(flash->ctx, slot_sector(target) + 1U) != 0)) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }

    s_ota.target      = target;
    s_ota.active_flag = 1U;
    s_ota.addr        = addr;
    s_ota.size        = size;
    s_ota.written     = 0U;
    s_ota.next_block  = 1U;

    if (cap < 4U) {
        return 0U;
    }
    /* lengthFormatIdentifier=0x20, max block=1024 */
    resp[0] = 0x74U;
    resp[1] = 0x20U;
    resp[2] = 0x04U;
    resp[3] = 0x00U;
    return 4U;
}

static uint16_t ota_transfer(const uint8_t *req, uint16_t len,
                             uint8_t *resp, uint16_t cap)
{
    const boot_flash_if_t *flash = boot_flash_stm32();
    uint16_t data_len;
    uint8_t  counter;

    if (s_ota.active_flag == 0U) {
        return ota_nrc(resp, cap, req[0], 0x22U);
    }
    if (len < 3U) {
        return ota_nrc(resp, cap, req[0], 0x13U);
    }

    counter = req[1];
    if (counter != s_ota.next_block) {
        return ota_nrc(resp, cap, req[0], 0x73U);
    }

    data_len = (uint16_t)(len - 2U);
    if ((data_len == 0U) || ((data_len & 3U) != 0U) ||
        ((s_ota.written + data_len) > s_ota.size)) {
        return ota_nrc(resp, cap, req[0], 0x13U);
    }
    if (flash == 0 || flash->write == 0 ||
        flash->write(flash->ctx, s_ota.addr + s_ota.written,
                     &req[2], data_len) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }

    s_ota.written += data_len;
    s_ota.next_block = (counter == 0xFFU) ? 0U : (uint8_t)(counter + 1U);

    if (cap < 2U) {
        return 0U;
    }
    resp[0] = 0x76U;
    resp[1] = counter;
    return 2U;
}

static uint16_t ota_exit(const uint8_t *req, uint16_t len,
                         uint8_t *resp, uint16_t cap)
{
    const boot_flash_if_t *flash = boot_flash_stm32();
    boot_app_header_t hdr;
    uint32_t vectors[2];

    if (s_ota.active_flag == 0U) {
        return ota_nrc(resp, cap, req[0], 0x22U);
    }
    if (len != 1U) {
        return ota_nrc(resp, cap, req[0], 0x13U);
    }
    if (s_ota.written != s_ota.size) {
        return ota_nrc(resp, cap, req[0], 0x13U);
    }

    if (flash->read(flash->ctx, s_ota.addr, &hdr, (uint32_t)sizeof hdr) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }
    if (boot_header_validate(&hdr, BOOT_APP_SLOT_SIZE) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }
    if (flash->read(flash->ctx, s_ota.addr + BOOT_APP_HEADER_SIZE,
                    vectors, (uint32_t)sizeof vectors) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }
    if (boot_vector_validate(vectors, s_ota.addr + BOOT_APP_HEADER_SIZE,
                             hdr.length) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }
    if (boot_image_validate_flash(flash, s_ota.addr + BOOT_APP_HEADER_SIZE,
                                  &hdr) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }

    if (Boot_StartTrial(s_ota.active, s_ota.target,
                        hdr.version, hdr.length, hdr.crc32) != 0) {
        return ota_nrc(resp, cap, req[0], 0x72U);
    }

    s_ota.active_flag = 0U;
    if (cap < 1U) {
        return 0U;
    }
    resp[0] = 0x77U;
    return 1U;
}

uint16_t OtaServer_Handle(const uint8_t *req, uint16_t req_len,
                          uint8_t *resp, uint16_t resp_cap)
{
    if (req == 0 || req_len == 0U || resp == 0 || resp_cap < 3U) {
        return 0U;
    }

    switch (req[0]) {
        case 0x34U:
            return ota_download(req, req_len, resp, resp_cap);
        case 0x36U:
            return ota_transfer(req, req_len, resp, resp_cap);
        case 0x37U:
            return ota_exit(req, req_len, resp, resp_cap);
        default:
            return ota_nrc(resp, resp_cap, req[0], 0x11U);
    }
}
