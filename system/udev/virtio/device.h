// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <magenta/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <threads.h>

namespace virtio {

class Device {
public:
    Device(mx_driver_t *driver, mx_device_t *bus_device);
    virtual ~Device();

    mx_device_t *bus_device() { return bus_device_; }
    mx_device_t *device() { return &device_; }

    virtual mx_status_t Bind(pci_protocol_t *, mx_handle_t pci_config_handle, const pci_config_t *);

    virtual mx_status_t Init() = 0;

    void StartIrqThread();

    // interrupt cases that devices may override
    virtual void IrqRingUpdate() {}
    virtual void IrqConfigChange() {}

    static Device* MXDeviceToObj(mx_device_t *dev) {
        return reinterpret_cast<Device *>((uintptr_t)dev - offsetof(Device, device_));
    }

    // used by Ring class to manipulate config registers
    void SetRing(uint16_t index, uint16_t count, mx_paddr_t pfn);
    void RingKick(uint16_t ring_index);

protected:
    // read bytes out of BAR 0's config space
    uint8_t ReadConfigBar(uint16_t offset);
    void WriteConfigBar(uint16_t offset, uint8_t val);
    mx_status_t CopyDeviceConfig(void *_buf, size_t len);

    void Reset();
    void StatusAcknowledgeDriver();
    void StatusDriverOK();

    static int IrqThreadEntry(void* arg);
    void IrqWorker();

    mx_driver_t *driver_ = nullptr;
    mx_device_t *bus_device_ = nullptr;

    // handles to pci bits
    pci_protocol_t *pci_ = nullptr;
    mx_handle_t pci_config_handle_ = 0;
    const pci_config_t *pci_config_ = nullptr;
    mx_handle_t irq_handle_ = 0;

    // bar0 memory map or PIO
    uint32_t bar0_pio_base_ = 0;
    uint32_t bar0_size_ = 0; // for now, must be set in subclass before Bind()
    volatile uint8_t* bar0_mmio_base_ = nullptr;
    mx_handle_t bar0_mmio_handle_ = 0;

    // irq thread object
    thrd_t irq_thread_ = {};

    // embedded device object
    mx_device_t device_ = {};
    mx_protocol_device_t device_ops_ = {};
};

} // namespace virtio
