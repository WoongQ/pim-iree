// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/PIM_device.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "iree/base/internal/arena.h"
#include "iree/base/internal/math.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/vulkan/api.h"
#include "iree/hal/drivers/vulkan/direct_command_buffer.h"
#include "iree/hal/drivers/vulkan/dynamic_symbols.h"
#include "iree/hal/drivers/vulkan/native_pipeline_layout.h"
#include "iree/hal/drivers/vulkan/native_semaphore.h"
#include "iree/hal/drivers/vulkan/nop_executable_cache.h"
#include "iree/hal/drivers/vulkan/status_util.h"
#include "iree/hal/drivers/vulkan/tracing.h"
#include "iree/hal/drivers/vulkan/util/arena.h"
#include "iree/hal/drivers/vulkan/util/ref_ptr.h"
#include "iree/hal/drivers/vulkan/PIM_allocator.h"
#include "iree/hal/drivers/vulkan/PIM_buffer.h"
#include "iree/hal/utils/buffer_transfer.h"

using namespace iree::hal::vulkan;


//===----------------------------------------------------------------------===//
// iree_hal_vulkan_device_t
//===----------------------------------------------------------------------===//

typedef struct iree_hal_vulkan_device_t {
  iree_hal_resource_t resource;
  iree_string_view_t identifier;

  // Optional driver that owns the instance. We retain it for our lifetime to
  // ensure the instance remains valid.
  iree_hal_driver_t* driver;

  // Flags overriding default device behavior.
  iree_hal_vulkan_device_flags_t flags;

  iree_allocator_t host_allocator;
  iree_hal_allocator_t* device_allocator;

#if defined(IREE_HAL_VULKAN_HAVE_RENDERDOC)
  RENDERDOC_API_LATEST* renderdoc_api;
#endif  // IREE_HAL_VULKAN_HAVE_RENDERDOC
} iree_hal_vulkan_device_t;

namespace {
extern const iree_hal_device_vtable_t iree_hal_vulkan_device_vtable;
}  // namespace

static iree_hal_vulkan_device_t* iree_hal_vulkan_device_cast(
    iree_hal_device_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_device_vtable);
  return (iree_hal_vulkan_device_t*)base_value;
}

IREE_API_EXPORT void iree_hal_vulkan_device_options_initialize(
    iree_hal_vulkan_device_options_t* out_options) {
  memset(out_options, 0, sizeof(*out_options));
  out_options->flags = 0;
  out_options->large_heap_block_size = 64 * 1024 * 1024;
}


static iree_status_t iree_hal_vulkan_device_create_internal(
    iree_hal_driver_t* driver, iree_string_view_t identifier,
    const iree_hal_vulkan_device_options_t* options,
    iree_allocator_t host_allocator, iree_hal_device_t** out_device) {
  

  iree_hal_vulkan_device_t* device = NULL;
  iree_host_size_t total_size =
      sizeof(*device) + identifier.size;
  IREE_RETURN_IF_ERROR(
      iree_allocator_malloc(host_allocator, total_size, (void**)&device));
  memset(device, 0, total_size);
  iree_hal_resource_initialize(&iree_hal_vulkan_device_vtable,
                               &device->resource);
  device->host_allocator = host_allocator;
  device->driver = driver;
  iree_hal_driver_retain(device->driver);
  uint8_t* buffer_ptr = (uint8_t*)device + sizeof(*device);
  buffer_ptr += iree_string_view_append_to_buffer(
      identifier, &device->identifier, (char*)buffer_ptr);
  device->flags = options->flags;

  // Create the device memory allocator that will service all buffer
  // allocation requests.
  iree_status_t status = iree_hal_vulkan_vma_allocator_create(
      host_allocator,
      (iree_hal_device_t*)device, &device->device_allocator);


  if (iree_status_is_ok(status)) {
    *out_device = (iree_hal_device_t*)device;
  } else {
    iree_hal_device_destroy((iree_hal_device_t*)device);
  }
  return status;
}

static void iree_hal_vulkan_device_destroy(iree_hal_device_t* base_device) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  iree_allocator_t host_allocator = iree_hal_device_host_allocator(base_device);
  IREE_TRACE_ZONE_BEGIN(z0);

  // There should be no more buffers live that use the allocator.
  iree_hal_allocator_release(device->device_allocator);

  // Finally, destroy the device.
  iree_hal_driver_release(device->driver);

  iree_allocator_free(host_allocator, device);

  IREE_TRACE_ZONE_END(z0);
}

iree_status_t iree_hal_vulkan_device_create(
    iree_hal_driver_t* driver, iree_string_view_t identifier,
    const iree_hal_vulkan_device_options_t* options,
    iree_allocator_t host_allocator,
    iree_hal_device_t** out_device) {
  

  // Allocate and initialize the device.
  iree_status_t status = iree_ok_status();
  //printf("\n [vulkan device create] before device create internal\n\n");

  if (iree_status_is_ok(status)) {
    status = iree_hal_vulkan_device_create_internal(
        driver, identifier, options,
        host_allocator, out_device);
  }
  //printf("\n [vulkan device create] after device create internal\n\n");
  
  return status;
}

static iree_string_view_t iree_hal_vulkan_device_id(
    iree_hal_device_t* base_device) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  return device->identifier;
}

static iree_allocator_t iree_hal_vulkan_device_host_allocator(
    iree_hal_device_t* base_device) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  return device->host_allocator;
}

static iree_hal_allocator_t* iree_hal_vulkan_device_allocator(
    iree_hal_device_t* base_device) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  return device->device_allocator;
}

static void iree_hal_vulkan_replace_device_allocator(
    iree_hal_device_t* base_device, iree_hal_allocator_t* new_allocator) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  iree_hal_allocator_retain(new_allocator);
  iree_hal_allocator_release(device->device_allocator);
  device->device_allocator = new_allocator;
}

static iree_status_t iree_hal_vulkan_device_trim(
    iree_hal_device_t* base_device) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  

  return iree_hal_allocator_trim(device->device_allocator);
}

static iree_status_t iree_hal_vulkan_device_query_i64(
    iree_hal_device_t* base_device, iree_string_view_t category,
    iree_string_view_t key, int64_t* out_value) {
  // iree_hal_vulkan_device_t* device =
  //     iree_hal_vulkan_device_cast(base_device);
  *out_value = 0;

  if (iree_string_view_equal(category,
                             iree_make_cstring_view("hal.executable.format"))) {
    *out_value =
        iree_string_view_equal(key, iree_make_cstring_view("pim-isr-fb"))
            ? 1
            : 0;
    return iree_ok_status();
  }

  return iree_make_status(
      IREE_STATUS_NOT_FOUND,
      "unknown device configuration key value '%.*s :: %.*s'",
      (int)category.size, category.data, (int)key.size, key.data);
}


static iree_status_t iree_hal_vulkan_device_create_channel(
    iree_hal_device_t* base_device, iree_hal_queue_affinity_t queue_affinity,
    iree_hal_channel_params_t params, iree_hal_channel_t** out_channel) {
  return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                          "collectives not implemented");
}

static iree_status_t iree_hal_vulkan_device_create_command_buffer(
    iree_hal_device_t* base_device, iree_hal_command_buffer_mode_t mode,
    iree_hal_command_category_t command_categories,
    iree_hal_queue_affinity_t queue_affinity, iree_host_size_t binding_capacity,
    iree_hal_command_buffer_t** out_command_buffer) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);

  
  command_categories |= IREE_HAL_COMMAND_CATEGORY_DISPATCH;


  return iree_hal_vulkan_direct_command_buffer_allocate(
      base_device, device->host_allocator, iree_hal_device_allocator(base_device), mode,
      command_categories, queue_affinity, binding_capacity,
      out_command_buffer);
}

static iree_status_t iree_hal_vulkan_device_create_descriptor_set_layout(
    iree_hal_device_t* base_device,
    iree_hal_descriptor_set_layout_flags_t flags,
    iree_host_size_t binding_count,
    const iree_hal_descriptor_set_layout_binding_t* bindings,
    iree_hal_descriptor_set_layout_t** out_descriptor_set_layout) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  
  (void)device;

  return iree_ok_status(); //iree_hal_vulkan_native_descriptor_set_layout_create()
  
}

static iree_status_t iree_hal_vulkan_device_create_event(
    iree_hal_device_t* base_device, iree_hal_event_t** out_event) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);

  (void)device;

  return iree_ok_status(); //iree_hal_vulkan_native_event_create(device->logical_device, out_event);
}

static iree_status_t iree_hal_vulkan_device_create_executable_cache(
    iree_hal_device_t* base_device, iree_string_view_t identifier,
    iree_loop_t loop, iree_hal_executable_cache_t** out_executable_cache) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  return iree_hal_vulkan_nop_executable_cache_create(
      device->host_allocator, identifier, out_executable_cache);
}

static iree_status_t iree_hal_vulkan_device_create_pipeline_layout(
    iree_hal_device_t* base_device, iree_host_size_t push_constants,
    iree_host_size_t set_layout_count,
    iree_hal_descriptor_set_layout_t* const* set_layouts,
    iree_hal_pipeline_layout_t** out_pipeline_layout) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  return iree_hal_vulkan_native_pipeline_layout_create(
      device->host_allocator, push_constants, set_layout_count, set_layouts,
      out_pipeline_layout);
}

static iree_status_t iree_hal_vulkan_device_create_semaphore(
    iree_hal_device_t* base_device, uint64_t initial_value,
    iree_hal_semaphore_t** out_semaphore) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  return iree_hal_vulkan_native_semaphore_create(device->host_allocator,
                                                 initial_value, out_semaphore);
}

static iree_hal_semaphore_compatibility_t
iree_hal_vulkan_device_query_semaphore_compatibility(
    iree_hal_device_t* base_device, iree_hal_semaphore_t* semaphore) {
  if (iree_hal_vulkan_native_semaphore_isa(semaphore)) {
    // Fast-path for semaphores related to this device.
    // TODO(benvanik): ensure the creating devices are compatible in cases where
    // multiple devices are used.
    return IREE_HAL_SEMAPHORE_COMPATIBILITY_ALL;
  }
  // TODO(benvanik): semaphore APIs for querying allowed export formats. We
  // can check device caps to see what external semaphore types are supported.
  return IREE_HAL_SEMAPHORE_COMPATIBILITY_HOST_ONLY;
}

static iree_status_t iree_hal_vulkan_device_queue_alloca(
    iree_hal_device_t* base_device, iree_hal_queue_affinity_t queue_affinity,
    const iree_hal_semaphore_list_t wait_semaphore_list,
    const iree_hal_semaphore_list_t signal_semaphore_list,
    iree_hal_allocator_pool_t pool, iree_hal_buffer_params_t params,
    iree_device_size_t allocation_size,
    iree_hal_buffer_t** IREE_RESTRICT out_buffer) {

  
  IREE_RETURN_IF_ERROR(iree_hal_allocator_allocate_buffer(
      iree_hal_device_allocator(base_device), params, allocation_size,
      iree_const_byte_span_empty(), out_buffer));

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_device_queue_dealloca(
    iree_hal_device_t* base_device, iree_hal_queue_affinity_t queue_affinity,
    const iree_hal_semaphore_list_t wait_semaphore_list,
    const iree_hal_semaphore_list_t signal_semaphore_list,
    iree_hal_buffer_t* buffer) {

  // only free PiM  
  
  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_device_queue_execute(
    iree_hal_device_t* base_device, iree_hal_queue_affinity_t queue_affinity,
    const iree_hal_semaphore_list_t wait_semaphore_list,
    const iree_hal_semaphore_list_t signal_semaphore_list,
    iree_host_size_t command_buffer_count,
    iree_hal_command_buffer_t* const* command_buffers) {

  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  // NOTE: today we are not discriminating queues based on command type.

  (void)device;
  
  return iree_ok_status();
  //return queue->Submit(1, &batch);
}

static iree_status_t iree_hal_vulkan_device_queue_flush(
    iree_hal_device_t* base_device, iree_hal_queue_affinity_t queue_affinity) {
  // Currently unused; we flush as submissions are made.
  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_device_wait_semaphores(
    iree_hal_device_t* base_device, iree_hal_wait_mode_t wait_mode,
    const iree_hal_semaphore_list_t semaphore_list, iree_timeout_t timeout) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  
  (void)device;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_device_profiling_begin(
    iree_hal_device_t* base_device,
    const iree_hal_device_profiling_options_t* options) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  
  (void)device;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_device_profiling_end(
    iree_hal_device_t* base_device) {
  iree_hal_vulkan_device_t* device = iree_hal_vulkan_device_cast(base_device);
  
  (void)device;

  return iree_ok_status();
}

namespace {
const iree_hal_device_vtable_t iree_hal_vulkan_device_vtable = {
    /*.destroy=*/iree_hal_vulkan_device_destroy,
    /*.id=*/iree_hal_vulkan_device_id,
    /*.host_allocator=*/iree_hal_vulkan_device_host_allocator,
    /*.device_allocator=*/iree_hal_vulkan_device_allocator,
    /*.replace_device_allocator=*/iree_hal_vulkan_replace_device_allocator,
    /*.trim=*/iree_hal_vulkan_device_trim,
    /*.query_i64=*/iree_hal_vulkan_device_query_i64,
    /*.create_channel=*/iree_hal_vulkan_device_create_channel,
    /*.create_command_buffer=*/iree_hal_vulkan_device_create_command_buffer,
    /*.create_descriptor_set_layout=*/
    iree_hal_vulkan_device_create_descriptor_set_layout,
    /*.create_event=*/iree_hal_vulkan_device_create_event,
    /*.create_executable_cache=*/
    iree_hal_vulkan_device_create_executable_cache,
    /*.create_pipeline_layout=*/
    iree_hal_vulkan_device_create_pipeline_layout,
    /*.create_semaphore=*/iree_hal_vulkan_device_create_semaphore,
    /*.query_semaphore_compatibility=*/
    iree_hal_vulkan_device_query_semaphore_compatibility,
    /*.transfer_range=*/iree_hal_device_submit_transfer_range_and_wait,
    /*.queue_alloca=*/iree_hal_vulkan_device_queue_alloca,
    /*.queue_dealloca=*/iree_hal_vulkan_device_queue_dealloca,
    /*.queue_execute=*/iree_hal_vulkan_device_queue_execute,
    /*.queue_flush=*/iree_hal_vulkan_device_queue_flush,
    /*.wait_semaphores=*/iree_hal_vulkan_device_wait_semaphores,
    /*.profiling_begin=*/iree_hal_vulkan_device_profiling_begin,
    /*.profiling_end=*/iree_hal_vulkan_device_profiling_end,
};
}  // namespace
