// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <stddef.h>
#include <magenta/types.h>

#include "virtio_ring.h"

namespace virtio {

class Device;

class Ring {
public:
    Ring(Device *device);
    ~Ring();

    mx_status_t Init(uint16_t index, uint16_t count);

    void FreeDesc(uint16_t desc_index);
    void FreeDescChain(uint16_t chain_head);
    uint16_t AllocDesc();
    struct vring_desc* AllocDescChain(uint16_t count, uint16_t *start_index);
    void SubmitChain(uint16_t desc_index);
    void Kick();

private:
    Device* device_ = nullptr;

    mx_paddr_t ring_pa_ = 0;
    void*      ring_ptr_ = nullptr;

    uint16_t index_ = 0;

    vring ring_ = {};
};

} // namespace virtio
