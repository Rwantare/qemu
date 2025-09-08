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

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "hw/core/irq.h"
#include "hw/sensor/adm1266.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"


static const uint8_t adm1266_ic_device_id[] = {0x03, 0x41, 0x12, 0x66};
static const uint8_t adm1266_ic_device_rev[] = {0x08, 0x01, 0x08, 0x07, 0x0,
                                                0x0, 0x07, 0x41, 0x30};

static void adm1266_clear_blackbox(ADM1266State *s)
{
    memset(s->blackbox_records, 0, sizeof(s->blackbox_records));
    for (int i = 0; i < ADM1266_BLACKBOX_RECORD_NUM_MAX; i++) {
        s->blackbox_records[i].flags = ADM1266_RECORD_EMPTY;
    }
    s->blackbox_info.record_count = 0;
    s->blackbox_info.logic_index = 0;
}

static void adm1266_exit_reset(Object *obj, ResetType type)
{
    ADM1266State *s = ADM1266(obj);
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);

    pmdev->page = 0;
    pmdev->capability = ADM1266_CAPABILITY_NO_PEC;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        pmdev->pages[i].operation = ADM1266_OPERATION_DEFAULT;
        pmdev->pages[i].revision = ADM1266_PMBUS_REVISION_DEFAULT;
        pmdev->pages[i].vout_mode = 0;
        pmdev->pages[i].read_vout = pmbus_data2linear_mode(12, 0);
        pmdev->pages[i].vout_margin_high = pmbus_data2linear_mode(15, 0);
        pmdev->pages[i].vout_margin_low = pmbus_data2linear_mode(3, 0);
        pmdev->pages[i].vout_ov_fault_limit = pmbus_data2linear_mode(16, 0);
        pmdev->pages[i].revision = ADM1266_PMBUS_REVISION_DEFAULT;
    }

    adm1266_clear_blackbox(s);
    for (int i = 0; i < ADM1266_RECORD_COUNT_DEFAULT; i++) {
        s->blackbox_records[i].id = cpu_to_le16(i);
        s->blackbox_records[i].flags = ADM1266_RECORD_USED;
        s->blackbox_records[i].powerup_counter = cpu_to_le16(i);
        s->blackbox_records[i].timestamp =
            cpu_to_le64((qemu_clock_get_ms(QEMU_CLOCK_REALTIME) / 1000) << 8);
        s->blackbox_info.logic_index = i;
    }
    s->partial_read = false;
    s->blackbox_info.record_count = ADM1266_RECORD_COUNT_DEFAULT;

    strncpy(s->mfr_id, ADM1266_MFR_ID_DEFAULT, 4);
    strncpy(s->mfr_model, ADM1266_MFR_MODEL_DEFAULT, 11);
    strncpy(s->mfr_rev, ADM1266_MFR_REVISION_DEFAULT, 3);
}

static uint8_t adm1266_read_byte(PMBusDevice *pmdev)
{
    ADM1266State *s = ADM1266(pmdev);
    uint8_t *raw_record;

    switch (pmdev->code) {
    case PMBUS_MFR_ID:                    /* R/W block */
        pmbus_send_string(pmdev, s->mfr_id);
        break;

    case PMBUS_MFR_MODEL:                 /* R/W block */
        pmbus_send_string(pmdev, s->mfr_model);
        break;

    case PMBUS_MFR_REVISION:              /* R/W block */
        pmbus_send_string(pmdev, s->mfr_rev);
        break;

    case PMBUS_IC_DEVICE_ID:
        pmbus_send(pmdev, adm1266_ic_device_id, sizeof(adm1266_ic_device_id));
        break;

    case PMBUS_IC_DEVICE_REV:
        pmbus_send(pmdev, adm1266_ic_device_rev, sizeof(adm1266_ic_device_rev));
        break;

    case ADM1266_BLACKBOX_CONFIG:
        pmbus_send16(pmdev, s->blackbox_config &
                     ADM1266_BLACKBOX_CONFIG_CYCLIC);
        break;

    case ADM1266_READ_BLACKBOX:
        raw_record =
                (uint8_t *)&s->blackbox_records[s->blackbox_info.logic_index];
        /* The full black box record exceeds SMBUS_DATA_MAX_LEN */
        if (!s->partial_read) {
            s->partial_read = true;
        } else {
            raw_record += sizeof(ADM1266BlackboxRecord) / 2;
            s->partial_read = false;
        }
        pmbus_send(pmdev, raw_record,
                   sizeof(ADM1266BlackboxRecord) / 2);         /* data */
        pmbus_send8(pmdev, sizeof(ADM1266BlackboxRecord) / 2); /* len */
        break;

    case ADM1266_BLACKBOX_INFORMATION:
        pmbus_send(pmdev, (const uint8_t *)&s->blackbox_info,
                   sizeof(s->blackbox_info));            /* data */
        pmbus_send8(pmdev, sizeof(ADM1266BlackboxInfo)); /* len */
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: reading from unimplemented register: 0x%02x\n",
                      __func__, pmdev->code);
        return 0xFF;
    }

    return 0;
}

static int adm1266_write_data(PMBusDevice *pmdev, const uint8_t *buf,
                              uint8_t len)
{
    ADM1266State *s = ADM1266(pmdev);
    uint8_t blackbox_request[2];

    switch (pmdev->code) {
    case PMBUS_MFR_ID:                    /* R/W block */
        pmbus_receive_block(pmdev, (uint8_t *)s->mfr_id, sizeof(s->mfr_id));
        break;

    case PMBUS_MFR_MODEL:                 /* R/W block */
        pmbus_receive_block(pmdev, (uint8_t *)s->mfr_model,
                            sizeof(s->mfr_model));
        break;

    case PMBUS_MFR_REVISION:               /* R/W block*/
        pmbus_receive_block(pmdev, (uint8_t *)s->mfr_rev, sizeof(s->mfr_rev));
        break;

    case ADM1266_BLACKBOX_CONFIG:
        s->blackbox_config = pmbus_receive16(pmdev) &
                                ADM1266_BLACKBOX_CONFIG_CYCLIC;
        break;

    /*
     * Writes to READ_BLACKBOX come in two forms.
     * Read requests are a two byte block write, {0x1, record_index}
     * Clearing records is a three byte block write, {0x2, 0xFE, 0x00}
     */
    case ADM1266_READ_BLACKBOX:
        blackbox_request[0] = pmbus_receive_block(pmdev,
                                (uint8_t *)&blackbox_request[1],
                                sizeof(blackbox_request));
        if (blackbox_request[0] == 1) {
            s->blackbox_info.logic_index = blackbox_request[1];
            s->partial_read = false;
            break;
        }
        /* erase blackbox records */
        if (blackbox_request[0] == 2 && blackbox_request[1] == 0xFE) {
            adm1266_clear_blackbox(s);
            break;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write to READ_BLACKBOX: 0x%02x%02x",
                      __func__, blackbox_request[0], blackbox_request[1]);
        break;

    case ADM1266_BLACKBOX_INFORMATION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register",
                      __func__);
        break;

    case ADM1266_SET_RTC:   /* do nothing */
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: writing to unimplemented register: 0x%02x\n",
                      __func__, pmdev->code);
        break;
    }
    return 0;
}

static void adm1266_get(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint16_t value;
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    PMBusVoutMode *mode = (PMBusVoutMode *)&pmdev->pages[0].vout_mode;

    if (strcmp(name, "vout") == 0) {
        value = pmbus_linear_mode2data(*(uint16_t *)opaque, mode->exp);
    } else {
        value = *(uint16_t *)opaque;
    }

    visit_type_uint16(v, name, &value, errp);
}

static void adm1266_set(Object *obj, Visitor *v, const char *name, void *opaque,
                        Error **errp)
{
    uint16_t *internal = opaque;
    uint16_t value;
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    PMBusVoutMode *mode = (PMBusVoutMode *)&pmdev->pages[0].vout_mode;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    *internal = pmbus_data2linear_mode(value, mode->exp);
    pmbus_check_limits(pmdev);
}

static const VMStateDescription vmstate_adm1266 = {
    .name = "ADM1266",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]){
        VMSTATE_PMBUS_DEVICE(parent, ADM1266State),
        VMSTATE_END_OF_LIST()
    }
};

static void adm1266_init(Object *obj)
{
    PMBusDevice *pmdev = PMBUS_DEVICE(obj);
    uint64_t flags = PB_HAS_VOUT_MODE | PB_HAS_VOUT | PB_HAS_VOUT_MARGIN |
                     PB_HAS_VOUT_RATING | PB_HAS_STATUS_MFR_SPECIFIC;

    for (int i = 0; i < ADM1266_NUM_PAGES; i++) {
        pmbus_page_config(pmdev, i, flags);

        object_property_add(obj, "vout[*]", "uint16",
                            adm1266_get,
                            adm1266_set, NULL, &pmdev->pages[i].read_vout);
    }
}

static void adm1266_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    dc->desc = "Analog Devices ADM1266 Hot Swap controller";
    dc->vmsd = &vmstate_adm1266;
    k->write_data = adm1266_write_data;
    k->receive_byte = adm1266_read_byte;
    k->device_num_pages = 17;

    rc->phases.exit = adm1266_exit_reset;
}

static const TypeInfo adm1266_info = {
    .name = TYPE_ADM1266,
    .parent = TYPE_PMBUS_DEVICE,
    .instance_size = sizeof(ADM1266State),
    .instance_init = adm1266_init,
    .class_init = adm1266_class_init,
};

static void adm1266_register_types(void)
{
    type_register_static(&adm1266_info);
}

type_init(adm1266_register_types)
