// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block.h"

#include <stdint.h>
#include <magenta/compiler.h>

#include "../trace.h"

#define LOCAL_TRACE 1

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
} __PACKED;

struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __PACKED;

#define VIRTIO_BLK_F_BARRIER  (1<<0)
#define VIRTIO_BLK_F_SIZE_MAX (1<<1)
#define VIRTIO_BLK_F_SEG_MAX  (1<<2)
#define VIRTIO_BLK_F_GEOMETRY (1<<4)
#define VIRTIO_BLK_F_RO       (1<<5)
#define VIRTIO_BLK_F_BLK_SIZE (1<<6)
#define VIRTIO_BLK_F_SCSI     (1<<7)
#define VIRTIO_BLK_F_FLUSH    (1<<9)
#define VIRTIO_BLK_F_TOPOLOGY (1<<10)
#define VIRTIO_BLK_F_CONFIG_WCE (1<<11)

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

namespace virtio {

// DDK level ops

// queue an iotxn. iotxn's are always completed by its complete() op
void BlockDevice::virtio_block_iotxn_queue(mx_device_t* dev, iotxn_t* txn)
{
    LTRACEF("dev %p, txn %p\n", dev, txn);

    // TODO: get a void * in the device structure so we dont need to do this
    Device *d = Device::MXDeviceToObj(dev);
    BlockDevice *bd = static_cast<BlockDevice *>(d);

    (void)bd;

    txn->ops->complete(txn, -1, 0);
}

// optional: return the size (in bytes) of the readable/writable space
// of the device.  Will default to 0 (non-seekable) if this is unimplemented
mx_off_t BlockDevice::virtio_block_get_size(mx_device_t* dev)
{
    LTRACEF("dev %p\n", dev);

    // TODO: get a void * in the device structure so we dont need to do this
    Device *d = Device::MXDeviceToObj(dev);
    BlockDevice *bd = static_cast<BlockDevice *>(d);

    return bd->GetSize();
}

BlockDevice::BlockDevice(mx_driver_t *driver, mx_device_t *bus_device)
    : Device(driver, bus_device)
{
    // so that Bind() knows how much io space to allocate
    bar0_size_ = 0x40;
}

BlockDevice::~BlockDevice() {}

mx_status_t BlockDevice::Init() {
    LTRACE_ENTRY;

    // reset the device
    Reset();

    // read our configuration
    CopyDeviceConfig(&config_, sizeof(config_));

    LTRACEF("capacity 0x%llx\n", config_.capacity);
    LTRACEF("size_max 0x%x\n", config_.size_max);
    LTRACEF("seg_max  0x%x\n", config_.seg_max);
    LTRACEF("blk_size 0x%x\n", config_.blk_size);

    // ack and set the driver status bit
    StatusAcknowledgeDriver();

    // XXX check features bits and ack/nak them

    // allocate the main vring
    auto err = vring_.Init(0, 128); // 128 matches legacy pci
    if (err < 0) {
        TRACEF("failed to allocate vring\n");
        return err;
    }

    // set DRIVER_OK
    StatusDriverOK();

    // initialize the mx_device and publish us
    device_ops_.iotxn_queue = &virtio_block_iotxn_queue;
    device_ops_.get_size = &virtio_block_get_size;
    device_init(&device_, driver_, "virtio-block", &device_ops_);

    device_.protocol_id = MX_PROTOCOL_BLOCK;
    auto status = device_add(&device_, bus_device_);
    if (status < 0)
        return status;

    return NO_ERROR;
}

} // namespace virtio
