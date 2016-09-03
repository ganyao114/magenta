// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "../device.h"
#include "../ring.h"

#include <magenta/compiler.h>
#include <stdlib.h>

namespace virtio {

class Ring;

class BlockDevice : public Device {
public:
    BlockDevice(mx_driver_t *driver, mx_device_t *device);
    virtual ~BlockDevice();

    virtual mx_status_t Init();

    uint64_t GetSize() const { return config_.capacity * config_.blk_size; }

private:
    // DDK driver hooks
    static void virtio_block_iotxn_queue(mx_device_t* dev, iotxn_t* txn);
    static mx_off_t virtio_block_get_size(mx_device_t* dev);

    // the main virtio ring
    Ring vring_ = { this };

    // saved block device configuration out of the pci config BAR
    struct virtio_blk_config {
        uint64_t capacity;
        uint32_t size_max;
        uint32_t seg_max;
        struct virtio_blk_geometry {
            uint16_t cylinders;
            uint8_t heads;
            uint8_t sectors;
        } geometry;
        uint32_t blk_size;
    } config_ __PACKED = {};
};

};
