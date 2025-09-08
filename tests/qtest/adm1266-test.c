/*
 * Analog Devices ADM1266 Cascadable Super Sequencer with Margin Control and
 * Fault Recording with PMBus
 *
 * Copyright 2022 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sensor/adm1266.h"

#include "hw/i2c/pmbus_device.h"
#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qemu/bswap.h"
#include "qobject/qdict.h"
#include "qobject/qnum.h"
#include "qemu/bitops.h"

#define TEST_ID "adm1266-test"
#define TEST_ADDR (0x12)

#define TEST_STRING_A                           "a sample"
#define TEST_STRING_B                           "b sample"
#define TEST_STRING_C                           "rev c"

static void compare_string(QI2CDevice *i2cdev, uint8_t reg,
                           const char *test_str)
{
    uint8_t len = i2c_get8(i2cdev, reg);
    char i2c_str[SMBUS_DATA_MAX_LEN] = {0};

    i2c_read_block(i2cdev, reg, (uint8_t *)i2c_str, len);
    g_assert_cmpstr(i2c_str, ==, test_str);
}

static void write_and_compare_string(QI2CDevice *i2cdev, uint8_t reg,
                                     const char *test_str, uint8_t len)
{
    char buf[SMBUS_DATA_MAX_LEN] = {0};
    buf[0] = len;
    strncpy(buf + 1, test_str, len);
    i2c_write_block(i2cdev, reg, (uint8_t *)buf, len + 1);
    compare_string(i2cdev, reg, test_str);
}

static void test_defaults(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t i2c_value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    i2c_value = i2c_get8(i2cdev, PMBUS_OPERATION);
    g_assert_cmphex(i2c_value, ==, ADM1266_OPERATION_DEFAULT);

    i2c_value = i2c_get8(i2cdev, PMBUS_REVISION);
    g_assert_cmphex(i2c_value, ==, ADM1266_PMBUS_REVISION_DEFAULT);

    compare_string(i2cdev, PMBUS_MFR_ID, ADM1266_MFR_ID_DEFAULT);
    compare_string(i2cdev, PMBUS_MFR_MODEL, ADM1266_MFR_MODEL_DEFAULT);
    compare_string(i2cdev, PMBUS_MFR_REVISION, ADM1266_MFR_REVISION_DEFAULT);
}

/* test r/w registers */
static void test_rw_regs(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    /* empty strings */
    i2c_set8(i2cdev, PMBUS_MFR_ID, 0);
    compare_string(i2cdev, PMBUS_MFR_ID, "");

    i2c_set8(i2cdev, PMBUS_MFR_MODEL, 0);
    compare_string(i2cdev, PMBUS_MFR_MODEL, "");

    i2c_set8(i2cdev, PMBUS_MFR_REVISION, 0);
    compare_string(i2cdev, PMBUS_MFR_REVISION, "");

    /* test strings */
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_A,
                             sizeof(TEST_STRING_A));
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_B,
                             sizeof(TEST_STRING_B));
    write_and_compare_string(i2cdev, PMBUS_MFR_ID, TEST_STRING_C,
                             sizeof(TEST_STRING_C));
}

static void read_blackbox(QI2CDevice *i2cdev, ADM1266BlackboxRecord *record,
                          uint8_t index)
{
    /* record is sent in two halves + len */
    uint8_t buf[2][(sizeof(ADM1266BlackboxRecord) / 2) + 1];
    uint8_t read_request[] = {0x1, index};

    i2c_write_block(i2cdev, ADM1266_READ_BLACKBOX, read_request,
                    sizeof(read_request));
    i2c_read_block(i2cdev, ADM1266_READ_BLACKBOX, buf[0], sizeof(buf[0]));
    i2c_read_block(i2cdev, ADM1266_READ_BLACKBOX, buf[1], sizeof(buf[1]));
    /* verify lengths */
    g_assert_cmpuint(buf[0][0] + buf[1][0], ==, sizeof(ADM1266BlackboxRecord));

    memcpy(record, &buf[0][1], sizeof(buf[0]) - 1);
    /* all the bytes in the second half are unused except the last one */
    record->crc = buf[1][sizeof(buf[1]) - 1];
}

/* test blackbox records */
static void test_blackbox(void *obj, void *data, QGuestAllocator *alloc)
{
    QI2CDevice *i2cdev = (QI2CDevice *)obj;
    uint8_t bb_info_buf[5];
    ADM1266BlackboxRecord blackbox_record;
    ADM1266BlackboxInfo *blackbox_info =
        (ADM1266BlackboxInfo *)(bb_info_buf + 1);
    uint8_t clear_request[] = {0x2, 0xFE, 0x00};
    uint8_t final_record;

    /* First, the blackbox shouldn't be empty, read blackbox information */
    i2c_read_block(i2cdev, ADM1266_BLACKBOX_INFORMATION, bb_info_buf,
                   sizeof(bb_info_buf));
    g_assert_cmpuint(bb_info_buf[0], ==, ADM1266_BLACKBOX_INFO_LEN);
    g_assert_cmpuint(blackbox_info->record_count, >, 0);
    g_assert_cmpuint(blackbox_info->logic_index, <,
                     blackbox_info->record_count);

    /* Get one of the records and check its index */
    /* Read final record */
    final_record = blackbox_info->record_count - 1;
    read_blackbox(i2cdev, &blackbox_record, final_record);

    /* Verify record ID matches what we expect */
    g_assert_cmpuint(le16_to_cpu(blackbox_record.id), ==, final_record);

    /* flags should be USED initially */
    g_assert_cmpuint(blackbox_record.flags, ==, ADM1266_RECORD_USED);

    /* Test clearing blackbox records */
    i2c_write_block(i2cdev, ADM1266_READ_BLACKBOX, clear_request,
                    sizeof(clear_request));

    /* Check info, record count should now be zero */
    i2c_read_block(i2cdev, ADM1266_BLACKBOX_INFORMATION, bb_info_buf,
                   sizeof(bb_info_buf));
    g_assert_cmpuint(bb_info_buf[0], ==, ADM1266_BLACKBOX_INFO_LEN);
    g_assert_cmpuint(blackbox_info->record_count, ==, 0);

    /* Read the same record again to verify the record is flagged as empty */
    read_blackbox(i2cdev, &blackbox_record, final_record);
    g_assert_cmpuint(blackbox_record.flags, ==, ADM1266_RECORD_EMPTY);
}

static void adm1266_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x12"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("adm1266", i2c_device_create);
    qos_node_consumes("adm1266", "i2c-bus", &opts);

    qos_add_test("test_defaults", "adm1266", test_defaults, NULL);
    qos_add_test("test_rw_regs", "adm1266", test_rw_regs, NULL);
    qos_add_test("test_blackbox", "adm1266", test_blackbox, NULL);
}

libqos_init(adm1266_register_nodes);
