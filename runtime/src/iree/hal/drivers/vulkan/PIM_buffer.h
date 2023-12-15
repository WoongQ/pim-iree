// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_VULKAN_VMA_BUFFER_H_
#define IREE_HAL_DRIVERS_VULKAN_VMA_BUFFER_H_

#include <vector>

#include "iree/base/api.h"
#include "iree/hal/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

iree_status_t iree_hal_PIM_buffer_wrap(
    iree_hal_allocator_t* allocator, iree_hal_memory_type_t memory_type,
    iree_hal_memory_access_t allowed_access,
    iree_hal_buffer_usage_t allowed_usage, iree_device_size_t allocation_size,
    iree_device_size_t byte_offset, iree_device_size_t byte_length,
    int PIM_addr, std::vector<int> PIM_dim, int PIM_rank,
    iree_hal_buffer_t** out_buffer);

// PiM SDK impl
int iree_hal_pim_buffer_get_PiM_addr(iree_hal_buffer_t* base_buffer);

void iree_hal_pim_buffer_push_PiM_addr(iree_hal_buffer_t* base_buffer, int new_PIM_addr);

void iree_hal_pim_buffer_push_PiM_dim(iree_hal_buffer_t* base_buffer, std::vector<int> new_PIM_dim);

std::vector<int> iree_hal_pim_buffer_get_PiM_dim(iree_hal_buffer_t* base_buffer);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_VULKAN_VMA_BUFFER_H_
