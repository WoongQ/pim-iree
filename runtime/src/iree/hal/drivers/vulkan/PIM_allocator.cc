// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/PIM_allocator.h"

#include <cstddef>
#include <cstring>
// for pim SDK
#include <vector>
#include <cmath>

#include "iree/base/api.h"
#include "iree/base/internal/math.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/vulkan/PIM_buffer.h"

// for pim SDK
#include "pim.h"
#include <iostream>

using namespace iree::hal::vulkan;

//===----------------------------------------------------------------------===//
// iree_hal_vulkan_vma_allocator_t
//===----------------------------------------------------------------------===//

typedef struct iree_hal_vulkan_vma_allocator_t {
  iree_hal_resource_t resource;
  iree_hal_device_t* device;  // unretained to avoid cycles
  iree_allocator_t host_allocator;


  IREE_STATISTICS(iree_hal_allocator_statistics_t statistics;)
} iree_hal_vulkan_vma_allocator_t;

namespace {
extern const iree_hal_allocator_vtable_t iree_hal_vulkan_vma_allocator_vtable;
}  // namespace

static iree_hal_vulkan_vma_allocator_t* iree_hal_vulkan_vma_allocator_cast(
    iree_hal_allocator_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_vma_allocator_vtable);
  return (iree_hal_vulkan_vma_allocator_t*)base_value;
}


iree_status_t iree_hal_vulkan_vma_allocator_create(
    iree_allocator_t host_allocator,
    iree_hal_device_t* device, iree_hal_allocator_t** out_allocator) {
  
  IREE_ASSERT_ARGUMENT(device);
  IREE_ASSERT_ARGUMENT(out_allocator);
  IREE_TRACE_ZONE_BEGIN(z0);
  
  iree_hal_vulkan_vma_allocator_t* allocator = NULL;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_allocator_malloc(host_allocator, sizeof(*allocator),
                                (void**)&allocator));
  iree_hal_resource_initialize(&iree_hal_vulkan_vma_allocator_vtable,
                               &allocator->resource);
  allocator->host_allocator = host_allocator;
  allocator->device = device;

  iree_status_t status = iree_ok_status();

  if (iree_status_is_ok(status)) {
    *out_allocator = (iree_hal_allocator_t*)allocator;
  } else {
    
  }

  IREE_TRACE_ZONE_END(z0);
  return iree_ok_status();
}

static void iree_hal_vulkan_vma_allocator_destroy(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator) {
  iree_hal_vulkan_vma_allocator_t* allocator =
      iree_hal_vulkan_vma_allocator_cast(base_allocator);
  iree_allocator_t host_allocator = allocator->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_allocator_free(host_allocator, allocator);

  IREE_TRACE_ZONE_END(z0);
}

static iree_allocator_t iree_hal_vulkan_vma_allocator_host_allocator(
    const iree_hal_allocator_t* IREE_RESTRICT base_allocator) {
  iree_hal_vulkan_vma_allocator_t* allocator =
      (iree_hal_vulkan_vma_allocator_t*)base_allocator;
  return allocator->host_allocator;
}

static iree_status_t iree_hal_vulkan_vma_allocator_trim(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator) {
  return iree_ok_status();
}

static void iree_hal_vulkan_vma_allocator_query_statistics(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator,
    iree_hal_allocator_statistics_t* IREE_RESTRICT out_statistics) {
  IREE_STATISTICS({
    iree_hal_vulkan_vma_allocator_t* allocator =
        iree_hal_vulkan_vma_allocator_cast(base_allocator);
    memcpy(out_statistics, &allocator->statistics, sizeof(*out_statistics));
  });
}

static iree_status_t iree_hal_vulkan_vma_allocator_query_memory_heaps(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator,
    iree_host_size_t capacity,
    iree_hal_allocator_memory_heap_t* IREE_RESTRICT heaps,
    iree_host_size_t* IREE_RESTRICT out_count) {
  IREE_TRACE_ZONE_BEGIN(z0);
  iree_hal_vulkan_vma_allocator_t* allocator =
      iree_hal_vulkan_vma_allocator_cast(base_allocator);

  (void)allocator;
  
  return iree_ok_status();
}

static iree_hal_buffer_compatibility_t
iree_hal_vulkan_vma_allocator_query_buffer_compatibility(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator,
    iree_hal_buffer_params_t* IREE_RESTRICT params,
    iree_device_size_t* IREE_RESTRICT allocation_size) {
  // TODO(benvanik): check to ensure the allocator can serve the memory type.

  // All buffers can be allocated on the heap.
  iree_hal_buffer_compatibility_t compatibility =
      IREE_HAL_BUFFER_COMPATIBILITY_ALLOCATABLE;

  if (iree_any_bit_set(params->usage, IREE_HAL_BUFFER_USAGE_TRANSFER)) {
    compatibility |= IREE_HAL_BUFFER_COMPATIBILITY_QUEUE_TRANSFER;
  }

  // Buffers can only be used on the queue if they are device visible.
  if (iree_all_bits_set(params->type, IREE_HAL_MEMORY_TYPE_DEVICE_VISIBLE)) {
    if (iree_any_bit_set(params->usage,
                         IREE_HAL_BUFFER_USAGE_DISPATCH_STORAGE)) {
      compatibility |= IREE_HAL_BUFFER_COMPATIBILITY_QUEUE_DISPATCH;
    }
  }

  // We are now optimal.
  params->type &= ~IREE_HAL_MEMORY_TYPE_OPTIMAL;

  // Guard against the corner case where the requested buffer size is 0. The
  // application is unlikely to do anything when requesting a 0-byte buffer; but
  // it can happen in real world use cases. So we should at least not crash.
  if (*allocation_size == 0) *allocation_size = 4;

  // Align allocation sizes to 4 bytes so shaders operating on 32 bit types can
  // act safely even on buffer ranges that are not naturally aligned.
  *allocation_size = iree_host_align(*allocation_size, 4);

  return compatibility;
}

static iree_status_t iree_hal_PIM_allocator_allocate_internal(
    iree_hal_vulkan_vma_allocator_t* IREE_RESTRICT allocator,
    const iree_hal_buffer_params_t* IREE_RESTRICT params,
    iree_device_size_t allocation_size, iree_const_byte_span_t initial_data, 
    int PiM_addr, std::vector<int> PiM_dim, int PiM_rank,
    iree_hal_buffer_t** IREE_RESTRICT out_buffer) {
  
  iree_hal_buffer_t* buffer = NULL;
  iree_status_t status = iree_hal_PIM_buffer_wrap(
      (iree_hal_allocator_t*)allocator, params->type, params->access,
      params->usage, allocation_size,
      /*byte_offset=*/0,
      /*byte_length=*/allocation_size,
      PiM_addr, PiM_dim, PiM_rank,
      &buffer);


  if (!iree_status_is_ok(status)) {
    return status;
  }
  
  if (iree_status_is_ok(status)) {
    *out_buffer = buffer;
  } else {
    iree_hal_buffer_release(buffer);
  }

  
  return status;
}

static iree_status_t iree_hal_PIM_allocator_allocate_buffer(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator,
    const iree_hal_buffer_params_t* IREE_RESTRICT params,
    iree_device_size_t allocation_size, iree_const_byte_span_t initial_data,
    iree_hal_buffer_t** IREE_RESTRICT out_buffer) {
  iree_hal_vulkan_vma_allocator_t* allocator =
      iree_hal_vulkan_vma_allocator_cast(base_allocator);

  // PIM device allocation
  // if there is no initial data, than generate zero data and allocate to device
  if (!iree_const_byte_span_is_empty(initial_data)) {
    
    int PiM_addr = PIM_SDK_alloc_buffer(allocation_size/4, (float*) initial_data.data);


    std::vector<int> PiM_dim;

    for(int i = 0; i<params->tensor_rank; i++){
      PiM_dim.push_back(params->tensor_shape[i]);
    }
    
    return iree_hal_PIM_allocator_allocate_internal(
      allocator, params, allocation_size, initial_data,
      PiM_addr, PiM_dim, params->tensor_rank,
      out_buffer);
  }
  else {

    std::vector<float> tmp_data(allocation_size/4, 0.0f);

    int PiM_addr = PIM_SDK_alloc_buffer(allocation_size/4, tmp_data.data());

    std::vector<int> PiM_dim = {0, 0, 0};
    
    return iree_hal_PIM_allocator_allocate_internal(
      allocator, params, allocation_size, initial_data,
      PiM_addr, PiM_dim, params->tensor_rank,
      out_buffer);
  }
}

static void iree_hal_vulkan_vma_allocator_deallocate_buffer(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator,
    iree_hal_buffer_t* IREE_RESTRICT base_buffer) {
  // VMA does the pooling for us so we don't need anything special.
  iree_hal_buffer_destroy(base_buffer);
}

static iree_status_t iree_hal_vulkan_vma_allocator_import_buffer(
    iree_hal_allocator_t* IREE_RESTRICT base_allocator,
    const iree_hal_buffer_params_t* IREE_RESTRICT params,
    iree_hal_external_buffer_t* IREE_RESTRICT external_buffer,
    iree_hal_buffer_release_callback_t release_callback,
    iree_hal_buffer_t** IREE_RESTRICT out_buffer) {
  // TODO(#7242): use VK_EXT_external_memory_host to import memory.
  return iree_make_status(IREE_STATUS_UNAVAILABLE,
                          "importing from external buffers not supported");
}


namespace {
const iree_hal_allocator_vtable_t iree_hal_vulkan_vma_allocator_vtable = {
    /*.destroy=*/iree_hal_vulkan_vma_allocator_destroy,
    /*.host_allocator=*/iree_hal_vulkan_vma_allocator_host_allocator,
    /*.trim=*/iree_hal_vulkan_vma_allocator_trim,
    /*.query_statistics=*/iree_hal_vulkan_vma_allocator_query_statistics,
    /*.query_memory_heaps=*/iree_hal_vulkan_vma_allocator_query_memory_heaps,
    /*.query_buffer_compatibility=*/
    iree_hal_vulkan_vma_allocator_query_buffer_compatibility,
    /*.allocate_buffer=*/iree_hal_PIM_allocator_allocate_buffer,
    /*.deallocate_buffer=*/iree_hal_vulkan_vma_allocator_deallocate_buffer,
    /*.import_buffer=*/iree_hal_vulkan_vma_allocator_import_buffer,
};
}  // namespace
