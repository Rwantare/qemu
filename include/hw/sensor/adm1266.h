/*
 * Analog Devices ADM1266 Cascadable Super Sequencer with Margin Control and
 * Fault Recording with PMBus
 *
 * https://www.analog.com/media/en/technical-documentation/data-sheets/adm1266.pdf
 *
 * Copyright 2023 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SENSOR_ADM1266_H
#define HW_SENSOR_ADM1266_H

#include "hw/i2c/pmbus_device.h"

#define TYPE_ADM1266 "adm1266"
OBJECT_DECLARE_SIMPLE_TYPE(ADM1266State, ADM1266)

#define ADM1266_BLACKBOX_CONFIG                 0xD3
#define     ADM1266_BLACKBOX_CONFIG_CYCLIC      0x01
#define ADM1266_PDIO_CONFIG                     0xD4
#define ADM1266_READ_STATE                      0xD9
#define ADM1266_READ_BLACKBOX                   0xDE
#define ADM1266_SET_RTC                         0xDF
#define ADM1266_GPIO_SYNC_CONFIGURATION         0xE1
#define ADM1266_BLACKBOX_INFORMATION            0xE6
#define ADM1266_PDIO_STATUS                     0xE9
#define ADM1266_GPIO_STATUS                     0xEA

/* Defaults */
#define ADM1266_OPERATION_DEFAULT               0x80
#define ADM1266_CAPABILITY_DEFAULT              0xA0
#define ADM1266_CAPABILITY_NO_PEC               0x20
#define ADM1266_PMBUS_REVISION_DEFAULT          0x22
#define ADM1266_MFR_ID_DEFAULT                  "ADI"
#define ADM1266_MFR_ID_DEFAULT_LEN              32
#define ADM1266_MFR_MODEL_DEFAULT               "ADM1266-A1"
#define ADM1266_MFR_MODEL_DEFAULT_LEN           32
#define ADM1266_MFR_REVISION_DEFAULT            "25"
#define ADM1266_MFR_REVISION_DEFAULT_LEN        8
#define ADM1266_RECORD_COUNT_DEFAULT            8

#define ADM1266_NUM_PAGES                       17

typedef struct ADM1266BlackboxInfo {
#define ADM1266_BLACKBOX_INFO_LEN         4
    uint8_t id;
    uint8_t reserved;
    uint8_t logic_index; /* Index of the last record read/written */
    uint8_t record_count;
} QEMU_PACKED ADM1266BlackboxInfo;
QEMU_BUILD_BUG_ON(sizeof(ADM1266BlackboxInfo) != ADM1266_BLACKBOX_INFO_LEN);

/* All the entries in the blackbox record are little-endian */
typedef struct ADM1266BlackboxRecord {
#define ADM1266_BLACKBOX_RECORD_LEN             64
#define ADM1266_BLACKBOX_RECORD_NUM_MAX         32
    uint16_t id;
#define ADM1266_RECORD_USED     0
#define ADM1266_RECORD_EMPTY    1
    uint8_t flags;
    uint8_t action_index;
    uint8_t rule_index;
    uint8_t voltage_status;     /* Undervoltage or overvoltage status */
    uint16_t current_state;
    uint16_t last_state;
    uint16_t vp_ov_status;      /* Overvoltage status of the VPx pins */
    uint16_t vp_uv_status;      /* Undervoltage status of the VPx pins */
    uint16_t gpio_in_status;
    uint16_t gpio_out_status;
    uint16_t pdio_in_status;
    uint16_t pdio_out_status;
    uint16_t powerup_counter;
    uint64_t timestamp;
    uint8_t reserved[31];
    uint8_t crc;
} QEMU_PACKED ADM1266BlackboxRecord;
QEMU_BUILD_BUG_ON(sizeof(ADM1266BlackboxRecord) != ADM1266_BLACKBOX_RECORD_LEN);

typedef struct ADM1266State {
    PMBusDevice parent;

    char mfr_id[32];
    char mfr_model[32];
    char mfr_rev[8];

    /* Blackbox state */
    bool partial_read;
    uint8_t record_index;
    uint16_t blackbox_config;
    ADM1266BlackboxInfo blackbox_info;
    /*
     * In single mode, the black box can store up to 32 fault records. When
     * full,no writes are accepted until the records are erased.
     * In cyclic mode, we still have 32 records, but they are overwritten in a
     * circular buffer.
     */
    ADM1266BlackboxRecord blackbox_records[ADM1266_BLACKBOX_RECORD_NUM_MAX];
} ADM1266State;

#endif /* HW_SENSOR_ADM1266_H */
