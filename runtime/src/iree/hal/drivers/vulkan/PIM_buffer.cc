// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/PIM_buffer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "iree/base/api.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/vulkan/status_util.h"

// pim SDK runtime
#include "pim.h"


typedef struct iree_hal_vulkan_vma_buffer_t {
  iree_hal_buffer_t base;

  int PIM_addr;

  // information of shape
  std::vector<int> PIM_dim;
  int PIM_rank;

} iree_hal_vulkan_vma_buffer_t;

namespace {
extern const iree_hal_buffer_vtable_t iree_hal_vulkan_vma_buffer_vtable;
}  // namespace

static iree_hal_vulkan_vma_buffer_t* iree_hal_vulkan_vma_buffer_cast(
    iree_hal_buffer_t* base_value) {
  
  // this causes error in PiM execution
  //IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_vma_buffer_vtable);
  return (iree_hal_vulkan_vma_buffer_t*)base_value;
}

iree_status_t iree_hal_PIM_buffer_wrap(
    iree_hal_allocator_t* allocator, iree_hal_memory_type_t memory_type,
    iree_hal_memory_access_t allowed_access,
    iree_hal_buffer_usage_t allowed_usage, iree_device_size_t allocation_size,
    iree_device_size_t byte_offset, iree_device_size_t byte_length,
    int PIM_addr, std::vector<int> PIM_dim, int PIM_rank,
    iree_hal_buffer_t** out_buffer) {
  IREE_ASSERT_ARGUMENT(allocator);
  IREE_ASSERT_ARGUMENT(out_buffer);
  IREE_TRACE_ZONE_BEGIN(z0);
  IREE_TRACE_ZONE_APPEND_VALUE(z0, (int64_t)allocation_size);

  iree_allocator_t host_allocator =
      iree_hal_allocator_host_allocator(allocator);
  iree_hal_vulkan_vma_buffer_t* buffer = NULL;
  iree_status_t status =
      iree_allocator_malloc(host_allocator, sizeof(*buffer), (void**)&buffer);
  if (iree_status_is_ok(status)) {
    iree_hal_buffer_initialize(
        host_allocator, allocator, &buffer->base, allocation_size, byte_offset,
        byte_length, memory_type, allowed_access, allowed_usage,
        &iree_hal_vulkan_vma_buffer_vtable, &buffer->base);
    
    buffer->PIM_addr = PIM_addr;
    buffer->PIM_dim = PIM_dim;
    buffer->PIM_rank = PIM_rank;

    *out_buffer = &buffer->base;
  } else {
    printf("\n PiM buffer alloc fail");
  }

  IREE_TRACE_ZONE_END(z0);
  return iree_ok_status();
}

// PiM SDK impl
int iree_hal_pim_buffer_get_PiM_addr(iree_hal_buffer_t* base_buffer){
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
    
  return buffer->PIM_addr;  
}

void iree_hal_pim_buffer_push_PiM_addr(iree_hal_buffer_t* base_buffer, int new_PIM_addr){
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
    
  buffer->PIM_addr = new_PIM_addr;  
}

void iree_hal_pim_buffer_push_PiM_dim(iree_hal_buffer_t* base_buffer, std::vector<int> new_PIM_dim){
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
    
  buffer->PIM_dim = new_PIM_dim;  
}

std::vector<int> iree_hal_pim_buffer_get_PiM_dim(iree_hal_buffer_t* base_buffer){
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
    
  return buffer->PIM_dim;  
}

static void iree_hal_vulkan_vma_buffer_destroy(iree_hal_buffer_t* base_buffer) {
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
  iree_allocator_t host_allocator = base_buffer->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);
  IREE_TRACE_ZONE_APPEND_VALUE(
      z0, (int64_t)iree_hal_buffer_allocation_size(base_buffer));

  iree_allocator_free(host_allocator, buffer);

  IREE_TRACE_ZONE_END(z0);
}


static iree_status_t iree_hal_vulkan_vma_buffer_map_range(
    iree_hal_buffer_t* base_buffer, iree_hal_mapping_mode_t mapping_mode,
    iree_hal_memory_access_t memory_access,
    iree_device_size_t local_byte_offset, iree_device_size_t local_byte_length,
    iree_hal_buffer_mapping_t* mapping) {
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);

  // return result per layer
  float* pim_output_tmp;

  IREE_RETURN_IF_ERROR(iree_allocator_malloc(base_buffer->host_allocator,
                        local_byte_length, (void**)&pim_output_tmp));

  get_PIM_SDK_buffer(buffer->PIM_addr, pim_output_tmp);

  mapping->contents =
      iree_make_byte_span(pim_output_tmp, local_byte_length);
  

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_vma_buffer_unmap_range(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length, iree_hal_buffer_mapping_t* mapping) {
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
  
  (void)buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_vma_buffer_invalidate_range(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length) {
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
  
  (void)buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_vma_buffer_flush_range(
    iree_hal_buffer_t* base_buffer, iree_device_size_t local_byte_offset,
    iree_device_size_t local_byte_length) {
  iree_hal_vulkan_vma_buffer_t* buffer =
      iree_hal_vulkan_vma_buffer_cast(base_buffer);
  
  (void)buffer;

  return iree_ok_status();
}

namespace {
const iree_hal_buffer_vtable_t iree_hal_vulkan_vma_buffer_vtable = {
    /*.recycle=*/iree_hal_buffer_recycle,
    /*.destroy=*/iree_hal_vulkan_vma_buffer_destroy,
    /*.map_range=*/iree_hal_vulkan_vma_buffer_map_range,
    /*.unmap_range=*/iree_hal_vulkan_vma_buffer_unmap_range,
    /*.invalidate_range=*/iree_hal_vulkan_vma_buffer_invalidate_range,
    /*.flush_range=*/iree_hal_vulkan_vma_buffer_flush_range,
};
}  // namespace
