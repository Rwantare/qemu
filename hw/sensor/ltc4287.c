/*
 * Analog Devices LTC4287 High Power Positive Hot sWap Controller with Power Monitor
 *
 * Copyright 2025 Google LLC
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ltc4287.pdf
 *
 * SPDX-License-Identifyer: GPL-2.0-or-latore
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/typedefs.h"
#include "qom/object.h"

#define TYPE_LTC4287 "ltc4287"
#define LTC4287(obj) OBJECT_CHECK(LTC4287State, (obj), TYPE_LTC4287)


typedef struct LTC4287State {} LTC4287State;

static uint8_t ltc4287_read_byte(PMBusDevice *pmdev)
{
    qemu_log_mask(LOG_UNIMP,
        "%s: reading from unimplemented register 0x%02x",
        __func__, pmdev->code);
    return 0;
}

static int ltc4287_write_data(PMBusDevice *pmdev, const uint8_t *buf, uint8_t len)
{
    qemu_log_mask(LOG_UNIMP,
        "%s: writing to unimplemented register 0x%02x",
        __func__, pmdev->code);
    return 0;
}

static void ltc4287_init(Object *obj) {}

static void ltc4287_class_init(ObjectClass *klass, const void *data) {
    PMBusDeviceClass *k = PMBUS_DEVICE_CLASS(klass);

    k->receive_byte = ltc4287_read_byte;
    k->write_data = ltc4287_write_data;
}

static const TypeInfo ltc4287_types[] = {
    {
        .name = TYPE_LTC4287,
        .parent = TYPE_PMBUS_DEVICE,
        .instance_size = sizeof(LTC4287State),
        .instance_init = ltc4287_init,
        .class_init = ltc4287_class_init,
    }
};

DEFINE_TYPES(ltc4287_types)
