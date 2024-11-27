/*
 * QTest testcase for the M25P80 Flash (Using the Aspeed SPI
 * Controller)
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "libqtest-single.h"
#include "qemu/bitops.h"

/*
 * ASPEED SPI Controller registers
 */
#define R_CONF              0x00
#define   CONF_ENABLE_W0       (1 << 16)
#define R_CE_CTRL           0x04
#define   CRTL_EXTENDED0       0  /* 32 bit addressing for SPI */
#define R_CTRL0             0x10
#define   CTRL_CE_STOP_ACTIVE  (1 << 2)
#define   CTRL_READMODE        0x0
#define   CTRL_FREADMODE       0x1
#define   CTRL_WRITEMODE       0x2
#define   CTRL_USERMODE        0x3
#define SR_WEL BIT(1)

/*
 * Flash commands
 */
enum {
    JEDEC_READ = 0x9f,
    RDSR = 0x5,
    WRDI = 0x4,
    BULK_ERASE = 0xc7,
    READ = 0x03,
    PP = 0x02,
    WRSR = 0x1,
    WREN = 0x6,
    SRWD = 0x80,
    RESET_ENABLE = 0x66,
    RESET_MEMORY = 0x99,
    EN_4BYTE_ADDR = 0xB7,
    ERASE_SECTOR = 0xd8,
};

#define FLASH_PAGE_SIZE           256

typedef struct TestData {
    QTestState *s;
    uint64_t spi_base;
    uint64_t flash_base;
    uint32_t jedec_id;
    char *tmp_path;
} TestData;

/*
 * Use an explicit bswap for the values read/wrote to the flash region
 * as they are BE and the Aspeed CPU is LE.
 */
static inline uint32_t make_be32(uint32_t data)
{
    return bswap32(data);
}

static inline void spi_writel(const TestData *data, uint64_t offset,
                              uint32_t value)
{
    qtest_writel(data->s, data->spi_base + offset, value);
}

static inline uint32_t spi_readl(const TestData *data, uint64_t offset)
{
    return qtest_readl(data->s, data->spi_base + offset);
}

static inline void flash_writeb(const TestData *data, uint64_t offset,
                                uint8_t value)
{
    qtest_writeb(data->s, data->flash_base + offset, value);
}

static inline void flash_writel(const TestData *data, uint64_t offset,
                                uint32_t value)
{
    qtest_writel(data->s, data->flash_base + offset, value);
}

static inline uint8_t flash_readb(const TestData *data, uint64_t offset)
{
    return qtest_readb(data->s, data->flash_base + offset);
}

static inline uint32_t flash_readl(const TestData *data, uint64_t offset)
{
    return qtest_readl(data->s, data->flash_base + offset);
}

static void spi_conf(const TestData *data, uint32_t value)
{
    uint32_t conf = spi_readl(data, R_CONF);

    conf |= value;
    spi_writel(data, R_CONF, conf);
}

static void spi_conf_remove(const TestData *data, uint32_t value)
{
    uint32_t conf = spi_readl(data, R_CONF);

    conf &= ~value;
    spi_writel(data, R_CONF, conf);
}

static void spi_ce_ctrl(const TestData *data, uint32_t value)
{
    uint32_t conf = spi_readl(data, R_CE_CTRL);

    conf |= value;
    spi_writel(data, R_CE_CTRL, conf);
}

static void spi_ctrl_setmode(const TestData *data, uint8_t mode, uint8_t cmd)
{
    uint32_t ctrl = spi_readl(data, R_CTRL0);
    ctrl &= ~(CTRL_USERMODE | 0xff << 16);
    ctrl |= mode | (cmd << 16);
    spi_writel(data, R_CTRL0, ctrl);
}

static void spi_ctrl_start_user(const TestData *data)
{
    uint32_t ctrl = spi_readl(data, R_CTRL0);

    ctrl |= CTRL_USERMODE | CTRL_CE_STOP_ACTIVE;
    spi_writel(data, R_CTRL0, ctrl);

    ctrl &= ~CTRL_CE_STOP_ACTIVE;
    spi_writel(data, R_CTRL0, ctrl);
}

static void spi_ctrl_stop_user(const TestData *data)
{
    uint32_t ctrl = spi_readl(data, R_CTRL0);

    ctrl |= CTRL_USERMODE | CTRL_CE_STOP_ACTIVE;
    spi_writel(data, R_CTRL0, ctrl);
}

static void flash_reset(const TestData *data)
{
    spi_conf(data, CONF_ENABLE_W0);

    spi_ctrl_start_user(data);
    flash_writeb(data, 0, RESET_ENABLE);
    flash_writeb(data, 0, RESET_MEMORY);
    flash_writeb(data, 0, WREN);
    flash_writeb(data, 0, BULK_ERASE);
    flash_writeb(data, 0, WRDI);
    spi_ctrl_stop_user(data);

    spi_conf_remove(data, CONF_ENABLE_W0);
}

static void test_read_jedec(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t jedec = 0x0;

    spi_conf(test_data, CONF_ENABLE_W0);

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, JEDEC_READ);
    jedec |= flash_readb(test_data, 0) << 16;
    jedec |= flash_readb(test_data, 0) << 8;
    jedec |= flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);

    flash_reset(test_data);

    g_assert_cmphex(jedec, ==, test_data->jedec_id);
}

static void read_page(const TestData *data, uint32_t addr, uint32_t *page)
{
    int i;

    spi_ctrl_start_user(data);

    flash_writeb(data, 0, EN_4BYTE_ADDR);
    flash_writeb(data, 0, READ);
    flash_writel(data, 0, make_be32(addr));

    /* Continuous read are supported */
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        page[i] = make_be32(flash_readl(data, 0));
    }
    spi_ctrl_stop_user(data);
}

static void read_page_mem(const TestData *data, uint32_t addr, uint32_t *page)
{
    int i;

    /* move out USER mode to use direct reads from the AHB bus */
    spi_ctrl_setmode(data, CTRL_READMODE, READ);

    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        page[i] = make_be32(flash_readl(data, addr + i * 4));
    }
}

static void write_page_mem(const TestData *data, uint32_t addr,
                           uint32_t write_value)
{
    spi_ctrl_setmode(data, CTRL_WRITEMODE, PP);

    for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        flash_writel(data, addr + i * 4, write_value);
    }
}

static void assert_page_mem(const TestData *data, uint32_t addr,
                            uint32_t expected_value)
{
    uint32_t page[FLASH_PAGE_SIZE / 4];
    read_page_mem(data, addr, page);
    for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, expected_value);
    }
}

static void test_erase_sector(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t some_page_addr = 0x600 * FLASH_PAGE_SIZE;
    uint32_t page[FLASH_PAGE_SIZE / 4];
    int i;

    spi_conf(test_data, CONF_ENABLE_W0);

    /*
     * Previous page should be full of 0xffs after backend is
     * initialized
     */
    read_page(test_data, some_page_addr - FLASH_PAGE_SIZE, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, EN_4BYTE_ADDR);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, PP);
    flash_writel(test_data, 0, make_be32(some_page_addr));

    /* Fill the page with its own addresses */
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        flash_writel(test_data, 0, make_be32(some_page_addr + i * 4));
    }
    spi_ctrl_stop_user(test_data);

    /* Check the page is correctly written */
    read_page(test_data, some_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, some_page_addr + i * 4);
    }

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, EN_4BYTE_ADDR);
    flash_writeb(test_data, 0, ERASE_SECTOR);
    flash_writel(test_data, 0, make_be32(some_page_addr));
    spi_ctrl_stop_user(test_data);

    /* Check the page is erased */
    read_page(test_data, some_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset(test_data);
}

static void test_erase_all(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t some_page_addr = 0x15000 * FLASH_PAGE_SIZE;
    uint32_t page[FLASH_PAGE_SIZE / 4];
    int i;

    spi_conf(test_data, CONF_ENABLE_W0);

    /*
     * Previous page should be full of 0xffs after backend is
     * initialized
     */
    read_page(test_data, some_page_addr - FLASH_PAGE_SIZE, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, EN_4BYTE_ADDR);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, PP);
    flash_writel(test_data, 0, make_be32(some_page_addr));

    /* Fill the page with its own addresses */
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        flash_writel(test_data, 0, make_be32(some_page_addr + i * 4));
    }
    spi_ctrl_stop_user(test_data);

    /* Check the page is correctly written */
    read_page(test_data, some_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, some_page_addr + i * 4);
    }

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, BULK_ERASE);
    spi_ctrl_stop_user(test_data);

    /* Check the page is erased */
    read_page(test_data, some_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset(test_data);
}

static void test_write_page(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t my_page_addr = 0x14000 * FLASH_PAGE_SIZE; /* beyond 16MB */
    uint32_t some_page_addr = 0x15000 * FLASH_PAGE_SIZE;
    uint32_t page[FLASH_PAGE_SIZE / 4];
    int i;

    spi_conf(test_data, CONF_ENABLE_W0);

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, EN_4BYTE_ADDR);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, PP);
    flash_writel(test_data, 0, make_be32(my_page_addr));

    /* Fill the page with its own addresses */
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        flash_writel(test_data, 0, make_be32(my_page_addr + i * 4));
    }
    spi_ctrl_stop_user(test_data);

    /* Check what was written */
    read_page(test_data, my_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, my_page_addr + i * 4);
    }

    /* Check some other page. It should be full of 0xff */
    read_page(test_data, some_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset(test_data);
}

static void test_read_page_mem(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t my_page_addr = 0x14000 * FLASH_PAGE_SIZE; /* beyond 16MB */
    uint32_t some_page_addr = 0x15000 * FLASH_PAGE_SIZE;
    uint32_t page[FLASH_PAGE_SIZE / 4];
    int i;

    /*
     * Enable 4BYTE mode for controller. This is should be strapped by
     * HW for CE0 anyhow.
     */
    spi_ce_ctrl(test_data, 1 << CRTL_EXTENDED0);

    /* Enable 4BYTE mode for flash. */
    spi_conf(test_data, CONF_ENABLE_W0);
    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, EN_4BYTE_ADDR);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, PP);
    flash_writel(test_data, 0, make_be32(my_page_addr));

    /* Fill the page with its own addresses */
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        flash_writel(test_data, 0, make_be32(my_page_addr + i * 4));
    }
    spi_ctrl_stop_user(test_data);
    spi_conf_remove(test_data, CONF_ENABLE_W0);

    /* Check what was written */
    read_page_mem(test_data, my_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, my_page_addr + i * 4);
    }

    /* Check some other page. It should be full of 0xff */
    read_page_mem(test_data, some_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, 0xffffffff);
    }

    flash_reset(test_data);
}

static void test_write_page_mem(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t my_page_addr = 0x15000 * FLASH_PAGE_SIZE;
    uint32_t page[FLASH_PAGE_SIZE / 4];
    int i;

    /*
     * Enable 4BYTE mode for controller. This is should be strapped by
     * HW for CE0 anyhow.
     */
    spi_ce_ctrl(test_data, 1 << CRTL_EXTENDED0);

    /* Enable 4BYTE mode for flash. */
    spi_conf(test_data, CONF_ENABLE_W0);
    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, EN_4BYTE_ADDR);
    flash_writeb(test_data, 0, WREN);
    spi_ctrl_stop_user(test_data);

    /* move out USER mode to use direct writes to the AHB bus */
    spi_ctrl_setmode(test_data, CTRL_WRITEMODE, PP);

    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        flash_writel(test_data, my_page_addr + i * 4,
               make_be32(my_page_addr + i * 4));
    }

    /* Check what was written */
    read_page_mem(test_data, my_page_addr, page);
    for (i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        g_assert_cmphex(page[i], ==, my_page_addr + i * 4);
    }

    flash_reset(test_data);
}

static void test_read_status_reg(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint8_t r;

    spi_conf(test_data, CONF_ENABLE_W0);

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);

    g_assert_cmphex(r & SR_WEL, ==, 0);
    g_assert(!qtest_qom_get_bool
            (test_data->s, "/machine/soc/fmc/ssi.0/child[0]", "write-enable"));

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WREN);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);

    g_assert_cmphex(r & SR_WEL, ==, SR_WEL);
    g_assert(qtest_qom_get_bool
            (test_data->s, "/machine/soc/fmc/ssi.0/child[0]", "write-enable"));

    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WRDI);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);

    g_assert_cmphex(r & SR_WEL, ==, 0);
    g_assert(!qtest_qom_get_bool
            (test_data->s, "/machine/soc/fmc/ssi.0/child[0]", "write-enable"));

    flash_reset(test_data);
}

static void test_status_reg_write_protection(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint8_t r;

    spi_conf(test_data, CONF_ENABLE_W0);

    /* default case: WP# is high and SRWD is low -> status register writable */
    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WREN);
    /* test ability to write SRWD */
    flash_writeb(test_data, 0, WRSR);
    flash_writeb(test_data, 0, SRWD);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);
    g_assert_cmphex(r & SRWD, ==, SRWD);

    /* WP# high and SRWD high -> status register writable */
    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WREN);
    /* test ability to write SRWD */
    flash_writeb(test_data, 0, WRSR);
    flash_writeb(test_data, 0, 0);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);
    g_assert_cmphex(r & SRWD, ==, 0);

    /* WP# low and SRWD low -> status register writable */
    qtest_set_irq_in(test_data->s,
                     "/machine/soc/fmc/ssi.0/child[0]", "WP#", 0, 0);
    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0, WREN);
    /* test ability to write SRWD */
    flash_writeb(test_data, 0, WRSR);
    flash_writeb(test_data, 0, SRWD);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);
    g_assert_cmphex(r & SRWD, ==, SRWD);

    /* WP# low and SRWD high -> status register NOT writable */
    spi_ctrl_start_user(test_data);
    flash_writeb(test_data, 0 , WREN);
    /* test ability to write SRWD */
    flash_writeb(test_data, 0, WRSR);
    flash_writeb(test_data, 0, 0);
    flash_writeb(test_data, 0, RDSR);
    r = flash_readb(test_data, 0);
    spi_ctrl_stop_user(test_data);
    /* write is not successful */
    g_assert_cmphex(r & SRWD, ==, SRWD);

    qtest_set_irq_in(test_data->s,
                     "/machine/soc/fmc/ssi.0/child[0]", "WP#", 0, 1);
    flash_reset(test_data);
}

static void test_write_block_protect(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t sector_size = 65536;
    uint32_t n_sectors = 512;

    spi_ce_ctrl(test_data, 1 << CRTL_EXTENDED0);
    spi_conf(test_data, CONF_ENABLE_W0);

    uint32_t bp_bits = 0b0;

    for (int i = 0; i < 16; i++) {
        bp_bits = ((i & 0b1000) << 3) | ((i & 0b0111) << 2);

        spi_ctrl_start_user(test_data);
        flash_writeb(test_data, 0, WREN);
        flash_writeb(test_data, 0, BULK_ERASE);
        flash_writeb(test_data, 0, WREN);
        flash_writeb(test_data, 0, WRSR);
        flash_writeb(test_data, 0, bp_bits);
        flash_writeb(test_data, 0, EN_4BYTE_ADDR);
        flash_writeb(test_data, 0, WREN);
        spi_ctrl_stop_user(test_data);

        uint32_t num_protected_sectors = i ? MIN(1 << (i - 1), n_sectors) : 0;
        uint32_t protection_start = n_sectors - num_protected_sectors;
        uint32_t protection_end = n_sectors;

        for (int sector = 0; sector < n_sectors; sector++) {
            uint32_t addr = sector * sector_size;

            assert_page_mem(test_data, addr, 0xffffffff);
            write_page_mem(test_data, addr, make_be32(0xabcdef12));

            uint32_t expected_value = protection_start <= sector
                                      && sector < protection_end
                                      ? 0xffffffff : 0xabcdef12;

            assert_page_mem(test_data, addr, expected_value);
        }
    }

    flash_reset(test_data);
}

static void test_write_block_protect_bottom_bit(const void *data)
{
    const TestData *test_data = (const TestData *)data;
    uint32_t sector_size = 65536;
    uint32_t n_sectors = 512;

    spi_ce_ctrl(test_data, 1 << CRTL_EXTENDED0);
    spi_conf(test_data, CONF_ENABLE_W0);

    /* top bottom bit is enabled */
    uint32_t bp_bits = 0b00100 << 3;

    for (int i = 0; i < 16; i++) {
        bp_bits = (((i & 0b1000) | 0b0100) << 3) | ((i & 0b0111) << 2);

        spi_ctrl_start_user(test_data);
        flash_writeb(test_data, 0, WREN);
        flash_writeb(test_data, 0, BULK_ERASE);
        flash_writeb(test_data, 0, WREN);
        flash_writeb(test_data, 0, WRSR);
        flash_writeb(test_data, 0, bp_bits);
        flash_writeb(test_data, 0, EN_4BYTE_ADDR);
        flash_writeb(test_data, 0, WREN);
        spi_ctrl_stop_user(test_data);

        uint32_t num_protected_sectors = i ? MIN(1 << (i - 1), n_sectors) : 0;
        uint32_t protection_start = 0;
        uint32_t protection_end = num_protected_sectors;

        for (int sector = 0; sector < n_sectors; sector++) {
            uint32_t addr = sector * sector_size;

            assert_page_mem(test_data, addr, 0xffffffff);
            write_page_mem(test_data, addr, make_be32(0xabcdef12));

            uint32_t expected_value = protection_start <= sector
                                      && sector < protection_end
                                      ? 0xffffffff : 0xabcdef12;

            assert_page_mem(test_data, addr, expected_value);
        }
    }

    flash_reset(test_data);
}

static void test_palmetto_bmc(TestData *data)
{
    int ret;
    int fd;

    fd = g_file_open_tmp("qtest.m25p80.n25q256a.XXXXXX", &data->tmp_path, NULL);
    g_assert(fd >= 0);
    ret = ftruncate(fd, 32 * 1024 * 1024);
    g_assert(ret == 0);
    close(fd);

    data->s = qtest_initf("-m 256 -machine palmetto-bmc "
                          "-drive file=%s,format=raw,if=mtd",
                          data->tmp_path);

    /* fmc cs0 with n25q256a flash */
    data->flash_base = 0x20000000;
    data->spi_base = 0x1E620000;
    data->jedec_id = 0x20ba19;

    qtest_add_data_func("/ast2400/smc/read_jedec", data, test_read_jedec);
    qtest_add_data_func("/ast2400/smc/erase_sector", data, test_erase_sector);
    qtest_add_data_func("/ast2400/smc/erase_all",  data, test_erase_all);
    qtest_add_data_func("/ast2400/smc/write_page", data, test_write_page);
    qtest_add_data_func("/ast2400/smc/read_page_mem",
                        data, test_read_page_mem);
    qtest_add_data_func("/ast2400/smc/write_page_mem",
                        data, test_write_page_mem);
    qtest_add_data_func("/ast2400/smc/read_status_reg",
                        data, test_read_status_reg);
    qtest_add_data_func("/ast2400/smc/status_reg_write_protection",
                        data, test_status_reg_write_protection);
    qtest_add_data_func("/ast2400/smc/write_block_protect",
                        data, test_write_block_protect);
    qtest_add_data_func("/ast2400/smc/write_block_protect_bottom_bit",
                        data, test_write_block_protect_bottom_bit);
}

int main(int argc, char **argv)
{
    TestData palmetto_data;
    int ret;

    g_test_init(&argc, &argv, NULL);

    test_palmetto_bmc(&palmetto_data);
    ret = g_test_run();

    qtest_quit(palmetto_data.s);
    unlink(palmetto_data.tmp_path);
    return ret;
}
