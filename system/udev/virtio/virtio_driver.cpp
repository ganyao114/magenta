// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/pci.h>

#include <mxtl/unique_ptr.h>

#include <magenta/compiler.h>
#include <magenta/new.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "trace.h"

#include "block/block.h"

#define LOCAL_TRACE 1

extern "C" ssize_t virtio_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    return 0;
}

extern "C" ssize_t virtio_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    return count;
}

#if 0
static mx_protocol_device_t virtio_device_proto = {
    .read = virtio_read,
    .write = virtio_write,
};
#endif

// implement driver object:

extern "C" mx_status_t virtio_bind(mx_driver_t* driver, mx_device_t* device) {
    LTRACEF("driver %p, device %p\n", driver, device);

    /* grab the pci device and configuration */
    pci_protocol_t* pci;
    if (device_get_protocol(device, MX_PROTOCOL_PCI, (void**)&pci)) {
        TRACEF("no pci protocol\n");
        return -1;
    }

    const pci_config_t *config;
    mx_handle_t config_handle = pci->get_config(device, &config);
    if (config_handle < 0) {
        TRACEF("failed to grab config handle\n");
        return -1;
    }

    LTRACEF("pci %p\n", pci);
    LTRACEF("0x%x:0x%x\n", config->vendor_id, config->device_id);

    mxtl::unique_ptr<virtio::Device> vd = nullptr;
    AllocChecker ac;
    switch (config->device_id) {
        case 0x1001:
            LTRACEF("found block device\n");
            vd.reset(new virtio::BlockDevice(driver, device));
            break;
        default:
            printf("unhandled device id, how did this happen?\n");
            return -1;
    }

    LTRACEF("calling Bind on driver\n");
    auto status = vd->Bind(pci, config_handle, config);
    if (status < 0)
        return status;

    status = vd->Init();
    if (status < 0)
        return status;

    // if we're here, we're successful so drop the unique ptr ref to the object and let it live on
    vd.release();

    LTRACE_EXIT;

    return NO_ERROR;
}


