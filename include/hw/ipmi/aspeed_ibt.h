/*
 * ASPEED iBT Device
 *
 * Copyright (c) 2016-2021 Cédric Le Goater, IBM Corporation.
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_IBT_H
#define ASPEED_IBT_H

#include "hw/ipmi/ipmi.h"
#include "hw/sysbus.h"

#define TYPE_ASPEED_IBT "aspeed.ibt"
#define ASPEED_IBT(obj) OBJECT_CHECK(AspeedIBTState, (obj), TYPE_ASPEED_IBT)

#define ASPEED_IBT_NR_REGS 7

#define ASPEED_IBT_BUFFER_SIZE 64

typedef struct AspeedIBTState {
    SysBusDevice parent;

    IPMICore *handler;

    uint8_t msg_id;
    uint8_t recv_msg[ASPEED_IBT_BUFFER_SIZE];
    uint8_t recv_msg_len;
    int recv_msg_index;

    uint8_t send_msg[ASPEED_IBT_BUFFER_SIZE];
    uint8_t send_msg_len;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t regs[ASPEED_IBT_NR_REGS];
} AspeedIBTState;


#endif /* ASPEED_IBT_H */
