/*
 * QTests for the LTC4287 device
 *
 * Copyright 2025 Google LLC
 *
 * SPDX-License-Identifer: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/i2c/pmbus_device.h"
#include "libqtest-single.h"
#include "libqos/i2c.h"
#include "libqos/qgraph.h"
#include "qobject/qdict.h"
#include "qobject/qnum.h"


#define TEST_ID     "ltc4287-test"
#define TEST_ADDR   0x87

/* test default values */
static void test_defaults(void *obj, void* data, QGuestAllocator *alloc)
{}

/* test fault registers */
static void test_fault_regs(void *obj, void* data, QGuestAllocator *alloc)
{}

static void ltc4287_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" TEST_ID ",address=0x87"
    };

    add_qi2c_address(&opts, &(QI2CAddress) { TEST_ADDR });

    qos_node_create_driver("ltc4287", i2c_device_create);
    qos_node_consumes("ltc4287", "i2c-bus", &opts);

    qos_add_test("test_defaults", "ltc4287", test_defaults, NULL);
    qos_add_test("test_fault_registers", "ltc4287", test_fault_regs, NULL);
}

libqos_init(ltc4287_register_nodes);
