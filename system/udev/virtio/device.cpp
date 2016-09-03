// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include <hw/inout.h>

#include "trace.h"
#include "virtio_priv.h"

#define LOCAL_TRACE 0

namespace virtio {

static mx_status_t virtio_device_release(mx_device_t* dev) {
    LTRACEF("mx_device_t %p\n", dev);

    Device *d = Device::MXDeviceToObj(dev);

    LTRACEF("virtio::Device %p\n", d);

    assert(0);

    return NO_ERROR;
}

Device::Device(mx_driver_t *driver, mx_device_t *bus_device)
    : driver_(driver), bus_device_(bus_device) {
    LTRACE_ENTRY;

    // set up some common device ops
    device_ops_.release = &virtio_device_release;
}

Device::~Device() {
    if (pci_config_handle_)
        mx_handle_close(pci_config_handle_);

    LTRACE_ENTRY;

    // TODO: close pci protocol and other handles

    if (irq_handle_ > 0)
        mx_handle_close(irq_handle_);
    if (bar0_mmio_handle_ > 0)
        mx_handle_close(bar0_mmio_handle_);
}

mx_status_t Device::Bind(pci_protocol_t *pci,
        mx_handle_t pci_config_handle, const pci_config_t *pci_config) {
    LTRACE_ENTRY;

    // save off handles to things
    pci_ = pci;
    pci_config_handle_ = pci_config_handle;
    pci_config_ = pci_config;

    // TODO: detect if we're transitional or not

    // claim the pci device
    mx_status_t r;
    r = pci->claim_device(bus_device_);
    if (r < 0)
        return r;

    // try to set up our IRQ mode
    if (pci->set_irq_mode(bus_device_, MX_PCIE_IRQ_MODE_MSI, 1)) {
        if (pci->set_irq_mode(bus_device_, MX_PCIE_IRQ_MODE_LEGACY, 1)) {
            TRACEF("failed to set irq mode\n");
            return -1;
        } else {
            TRACEF("using legacy irq mode\n");
        }
    }
    irq_handle_ = pci->map_interrupt(bus_device_, 0);
    if (irq_handle_ < 0) {
        TRACEF("failed to map irq\n");
        return -1;
    }

    LTRACEF("irq handle %u\n", irq_handle_);

    // look at BAR0, which should be a PIO memory window
    bar0_pio_base_ = pci_config->base_addresses[0];
    LTRACEF("BAR0 address %#x\n", bar0_pio_base_);
    if ((bar0_pio_base_ & 0x1) == 0) {
        TRACEF("bar 0 does not appear to be PIO (address %#x, aborting\n", bar0_pio_base_);
        return -1;
    }

    bar0_pio_base_ &= ~1;
    if (bar0_pio_base_ > 0xffff) {
        bar0_pio_base_ = 0;

        // this may be a PIO mapped as mmio (non x86 host)
        // map in the mmio space
        // XXX this seems to be broken right now
        uint64_t sz;
        bar0_mmio_handle_ = pci->map_mmio(bus_device_, 0, MX_CACHE_POLICY_UNCACHED_DEVICE, (void **)&bar0_mmio_base_, &sz);
        if (bar0_mmio_handle_ != NO_ERROR) {
            TRACEF("cannot map io %d\n", bar0_mmio_handle_);
            return bar0_mmio_handle_;
        }

        LTRACEF("bar0_mmio_base_ %p, sz %#llx\n", bar0_mmio_base_, sz);
    } else {
        // this is probably PIO
        r = mx_mmap_device_io(get_root_resource(), bar0_pio_base_, bar0_size_);
        if (r != NO_ERROR) {
            TRACEF("failed to access PIO range %#x, length %#xw\n", bar0_pio_base_, bar0_size_);
            return r;
        }
    }

    // enable bus mastering
    if ((r = pci->enable_bus_master(bus_device_, true)) < 0) {
        TRACEF("cannot enable bus master %d\n", r);
        return -1;
    }

    LTRACE_EXIT;

    return NO_ERROR;
}

void Device::IrqWorker() {
    LTRACEF("started\n");
    for (;;) {
        mx_status_t r;
        if ((r = mx_pci_interrupt_wait(irq_handle_)) < 0) {
            printf("virtio: irq wait failed? %d\n", r);
            break;
        }

        uint8_t irq_status = inp((bar0_pio_base_+ VIRTIO_PCI_ISR_STATUS) & 0xffff);
        LTRACEF("irq_status %u\n", irq_status);

        if (irq_status & 0x1) { /* used ring update */
            IrqRingUpdate();

#if 0
            /* cycle through all the active rings */
            for (uint r = 0; r < MAX_VIRTIO_RINGS; r++) {
                if ((dev->active_rings_bitmap & (1<<r)) == 0)
                    continue;

                struct vring *ring = &dev->ring[r];
                LTRACEF("ring %u: used flags 0x%hhx idx 0x%hhx last_used %u\n",
                        r, ring->used->flags, ring->used->idx, ring->last_used);

                uint cur_idx = ring->used->idx;
                for (uint i = ring->last_used; i != (cur_idx & ring->num_mask); i = (i + 1) & ring->num_mask) {
                    LTRACEF("looking at idx %u\n", i);

                    // process chain
                    struct vring_used_elem *used_elem = &ring->used->ring[i];
                    LTRACEF("id %u, len %u\n", used_elem->id, used_elem->len);

                    DEBUG_ASSERT(dev->irq_driver_callback);
                    ret |= dev->irq_driver_callback(dev, r, used_elem);

                    ring->last_used = (ring->last_used + 1) & ring->num_mask;
                }
            }
#endif
        }
        if (irq_status & 0x2) { /* config change */
            IrqConfigChange();
        }

#if 0
        mtx_lock(&edev->lock);
        if (eth_handle_irq(&edev->eth) & ETH_IRQ_RX) {
            device_state_set(&edev->dev, DEV_STATE_READABLE);
        }
        mtx_unlock(&edev->lock);
#endif
    }
}

int Device::IrqThreadEntry(void* arg) {
    Device *d = static_cast<Device *>(arg);

    d->IrqWorker();

    return 0;
}

void Device::StartIrqThread()
{
    thrd_create_with_name(&irq_thread_, IrqThreadEntry, this, "virtio-thread");
    thrd_detach(irq_thread_);
}

uint8_t Device::ReadConfigBar(uint16_t offset)
{
    if (bar0_pio_base_) {
        uint16_t port = (bar0_pio_base_ + offset) & 0xffff;
        //LTRACEF("port %#x\n", port);
        return inp(port);
    } else {
        // XXX implement
        assert(0);
        return 0;
    }
}

void Device::WriteConfigBar(uint16_t offset, uint8_t val)
{
    if (bar0_pio_base_) {
        uint16_t port = (bar0_pio_base_ + offset) & 0xffff;
        //LTRACEF("port %#x\n", port);
        outp(port, val);
    } else {
        // XXX implement
        assert(0);
    }
}

mx_status_t Device::CopyDeviceConfig(void *_buf, size_t len)
{
    // XXX handle MSI vs noMSI
    size_t offset = VIRTIO_PCI_CONFIG_OFFSET_NOMSI;

    uint8_t *buf = (uint8_t *)_buf;
    for (size_t i = 0; i < len; i++) {
        if (bar0_pio_base_) {
            buf[i] = ReadConfigBar((offset + i) & 0xffff);
        } else {
            // XXX implement
            assert(0);
        }
    }

    return NO_ERROR;
}

void Device::SetRing(uint16_t index, uint16_t count, mx_paddr_t pa)
{
    LTRACEF("index %u, count %u, pa %#lx\n", index, count, pa);
    if (bar0_pio_base_) {
        outpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_SELECT) & 0xffff, index);
        outpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_SIZE) & 0xffff, count);
        outpd((bar0_pio_base_ + VIRTIO_PCI_QUEUE_PFN) & 0xffff, (uint32_t)(pa / PAGE_SIZE));
    } else {
        // XXX implement
        assert(0);
    }
}

void Device::RingKick(uint16_t ring_index)
{
    LTRACEF("index %u\n", ring_index);
    if (bar0_pio_base_) {
        outpw((bar0_pio_base_ + VIRTIO_PCI_QUEUE_NOTIFY) & 0xffff, ring_index);
    } else {
        // XXX implement
        assert(0);
    }
}

void Device::Reset()
{
    WriteConfigBar(VIRTIO_PCI_DEVICE_STATUS, 0);
}

void Device::StatusAcknowledgeDriver()
{
    uint8_t val = ReadConfigBar(VIRTIO_PCI_DEVICE_STATUS);
    val |= VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
    WriteConfigBar(VIRTIO_PCI_DEVICE_STATUS, val);
}

void Device::StatusDriverOK()
{
    uint8_t val = ReadConfigBar(VIRTIO_PCI_DEVICE_STATUS);
    val |= VIRTIO_STATUS_DRIVER_OK;
    WriteConfigBar(VIRTIO_PCI_DEVICE_STATUS, val);
}


};
