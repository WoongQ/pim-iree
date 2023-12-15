// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/direct_command_buffer.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <bitset>
#include <algorithm>
#include <unistd.h>

#include "iree/base/api.h"
#include "iree/base/internal/inline_array.h"
#include "iree/base/internal/math.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/vulkan/dynamic_symbols.h"
#include "iree/hal/drivers/vulkan/native_executable.h"
#include "iree/hal/drivers/vulkan/native_pipeline_layout.h"
#include "iree/hal/drivers/vulkan/status_util.h"
#include "iree/hal/drivers/vulkan/util/ref_ptr.h"
#include "iree/hal/drivers/vulkan/PIM_buffer.h"
#include "iree/hal/utils/resource_set.h"

// pim SDK runtime
#include "pim.h"


using namespace iree::hal::vulkan;


// Command buffer implementation that directly maps to VkCommandBuffer.
// This records the commands on the calling thread without additional threading
// indirection.
typedef struct iree_hal_vulkan_direct_command_buffer_t {
  iree_hal_command_buffer_t base;

  iree_allocator_t host_allocator;
  iree_hal_allocator_t* device_allocator;  

  std::vector<int> buffer_addr_vec;
  std::vector<std::vector<int>> buffer_shape_vec;
  
  iree_hal_buffer_t* input_PIM_buffer;
  iree_hal_buffer_t* result_PIM_buffer;

} iree_hal_vulkan_direct_command_buffer_t;

namespace {
extern const iree_hal_command_buffer_vtable_t
    iree_hal_vulkan_direct_command_buffer_vtable;
}  // namespace

static iree_hal_vulkan_direct_command_buffer_t*
iree_hal_vulkan_direct_command_buffer_cast(
    iree_hal_command_buffer_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value,
                       &iree_hal_vulkan_direct_command_buffer_vtable);
  return (iree_hal_vulkan_direct_command_buffer_t*)base_value;
}

iree_status_t iree_hal_vulkan_direct_command_buffer_allocate(
    iree_hal_device_t* device,
    iree_allocator_t host_allocator, iree_hal_allocator_t* device_allocator,
    iree_hal_command_buffer_mode_t mode,
    iree_hal_command_category_t command_categories,
    iree_hal_queue_affinity_t queue_affinity, iree_host_size_t binding_capacity,
    iree_hal_command_buffer_t** out_command_buffer) {
  
  IREE_ASSERT_ARGUMENT(out_command_buffer);
  *out_command_buffer = NULL;

  if (binding_capacity > 0) {
    // TODO(#10144): support indirect command buffers with binding tables.
    return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                            "indirect command buffers not yet implemented");
  }

  IREE_TRACE_ZONE_BEGIN(z0);

  iree_hal_vulkan_direct_command_buffer_t* command_buffer = NULL;
  iree_status_t status =
      iree_allocator_malloc(host_allocator,
                            sizeof(*command_buffer), (void**)&command_buffer);
  if (iree_status_is_ok(status)) {
    iree_hal_command_buffer_initialize(
        device, mode, command_categories, queue_affinity, binding_capacity,
        &iree_hal_vulkan_direct_command_buffer_vtable, &command_buffer->base);    
    
    command_buffer->host_allocator = host_allocator; 
    command_buffer->device_allocator = device_allocator;        
    
    status = iree_ok_status();
  }

  if (iree_status_is_ok(status)) {
    *out_command_buffer = &command_buffer->base;
  } else {
    
  }

  IREE_TRACE_ZONE_END(z0);
  return status;
}

bool iree_hal_vulkan_direct_command_buffer_isa(
    iree_hal_command_buffer_t* command_buffer) {
  return iree_hal_command_buffer_dyn_cast(
      command_buffer, &iree_hal_vulkan_direct_command_buffer_vtable);
}

static void* iree_hal_vulkan_direct_command_buffer_dyn_cast(
    iree_hal_command_buffer_t* command_buffer, const void* vtable) {
  if (vtable == &iree_hal_vulkan_direct_command_buffer_vtable) {
    IREE_HAL_ASSERT_TYPE(command_buffer, vtable);
    return command_buffer;
  }
  return NULL;
}

static void iree_hal_vulkan_direct_command_buffer_destroy(
    iree_hal_command_buffer_t* base_command_buffer) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;
}



static iree_status_t iree_hal_vulkan_direct_command_buffer_begin(
    iree_hal_command_buffer_t* base_command_buffer) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_end(
    iree_hal_command_buffer_t* base_command_buffer) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void)command_buffer;

  return iree_ok_status();
}

static void iree_hal_vulkan_direct_command_buffer_begin_debug_group(
    iree_hal_command_buffer_t* base_command_buffer, iree_string_view_t label,
    iree_hal_label_color_t label_color,
    const iree_hal_label_location_t* location) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;
}

static void iree_hal_vulkan_direct_command_buffer_end_debug_group(
    iree_hal_command_buffer_t* base_command_buffer) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;
}


static iree_status_t iree_hal_vulkan_direct_command_buffer_execution_barrier(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_execution_stage_t source_stage_mask,
    iree_hal_execution_stage_t target_stage_mask,
    iree_hal_execution_barrier_flags_t flags,
    iree_host_size_t memory_barrier_count,
    const iree_hal_memory_barrier_t* memory_barriers,
    iree_host_size_t buffer_barrier_count,
    const iree_hal_buffer_barrier_t* buffer_barriers) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_signal_event(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_event_t* event,
    iree_hal_execution_stage_t source_stage_mask) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_reset_event(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_event_t* event,
    iree_hal_execution_stage_t source_stage_mask) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_wait_events(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_host_size_t event_count, const iree_hal_event_t** events,
    iree_hal_execution_stage_t source_stage_mask,
    iree_hal_execution_stage_t target_stage_mask,
    iree_host_size_t memory_barrier_count,
    const iree_hal_memory_barrier_t* memory_barriers,
    iree_host_size_t buffer_barrier_count,
    const iree_hal_buffer_barrier_t* buffer_barriers) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_discard_buffer(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_buffer_t* buffer) {
  // NOTE: we could use this to prevent queue family transitions.
  return iree_ok_status();
}


static iree_status_t iree_hal_vulkan_direct_command_buffer_fill_buffer(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_buffer_t* target_buffer, iree_device_size_t target_offset,
    iree_device_size_t length, const void* pattern,
    iree_host_size_t pattern_length) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_update_buffer(
    iree_hal_command_buffer_t* base_command_buffer, const void* source_buffer,
    iree_host_size_t source_offset, iree_hal_buffer_t* target_buffer,
    iree_device_size_t target_offset, iree_device_size_t length) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_copy_buffer(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_buffer_t* source_buffer, iree_device_size_t source_offset,
    iree_hal_buffer_t* target_buffer, iree_device_size_t target_offset,
    iree_device_size_t length) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);
  
  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_collective(
    iree_hal_command_buffer_t* base_command_buffer, iree_hal_channel_t* channel,
    iree_hal_collective_op_t op, uint32_t param,
    iree_hal_buffer_binding_t send_binding,
    iree_hal_buffer_binding_t recv_binding, iree_device_size_t element_count) {
  return iree_make_status(IREE_STATUS_UNIMPLEMENTED,
                          "collectives not yet implemented on Vulkan");
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_push_constants(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_pipeline_layout_t* pipeline_layout, iree_host_size_t offset,
    const void* values, iree_host_size_t values_length) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_push_descriptor_set(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_pipeline_layout_t* pipeline_layout, uint32_t set,
    iree_host_size_t binding_count,
    const iree_hal_descriptor_set_binding_t* bindings) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void) command_buffer;

  command_buffer->input_PIM_buffer = bindings[0].buffer;
  command_buffer->result_PIM_buffer = bindings[binding_count-1].buffer;

  // clear previous bindings
  command_buffer->buffer_addr_vec.clear();
  command_buffer->buffer_addr_vec.shrink_to_fit();

  for( int i=0; i<binding_count; i++){
    command_buffer->buffer_addr_vec.push_back(iree_hal_pim_buffer_get_PiM_addr(bindings[i].buffer));
  }

  // clear previous bindings
  command_buffer->buffer_shape_vec.clear();
  command_buffer->buffer_shape_vec.shrink_to_fit();

  for( int i=0; i<binding_count; i++){
    command_buffer->buffer_shape_vec.push_back(iree_hal_pim_buffer_get_PiM_dim(bindings[i].buffer));
  }
  
  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_dispatch(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_executable_t* executable, int32_t entry_point,
    uint32_t workgroup_x, uint32_t workgroup_y, uint32_t workgroup_z) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  // PiM execution information decoding
  int code_length = iree_hal_pim_executable_cmd_len(executable);

  // if there is no device_code, than skip dispatch
  if(code_length == 0){    
    return iree_ok_status();
  }

  // get PiM device code to dispatch
  uint64_t PiM_code = *(uint64_t*)(iree_hal_pim_executable_cmd_get(executable));
  
  std::vector<int> output_shape;
  int return_addr = PIM_dispatch_code(command_buffer->buffer_addr_vec, PiM_code, command_buffer->buffer_shape_vec, output_shape);

  // update hal_PiM_buffer
  iree_hal_pim_buffer_push_PiM_addr(command_buffer->result_PIM_buffer, return_addr);

  iree_hal_pim_buffer_push_PiM_dim(command_buffer->result_PIM_buffer, output_shape);

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_dispatch_indirect(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_executable_t* executable, int32_t entry_point,
    iree_hal_buffer_t* workgroups_buffer,
    iree_device_size_t workgroups_offset) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

  (void)command_buffer;

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_direct_command_buffer_execute_commands(
    iree_hal_command_buffer_t* base_command_buffer,
    iree_hal_command_buffer_t* base_commands,
    iree_hal_buffer_binding_table_t binding_table) {
  iree_hal_vulkan_direct_command_buffer_t* command_buffer =
      iree_hal_vulkan_direct_command_buffer_cast(base_command_buffer);

    (void)command_buffer;

  return iree_ok_status();
}

namespace {
const iree_hal_command_buffer_vtable_t
    iree_hal_vulkan_direct_command_buffer_vtable = {
        /*.destroy=*/iree_hal_vulkan_direct_command_buffer_destroy,
        /*.dyn_cast=*/iree_hal_vulkan_direct_command_buffer_dyn_cast,
        /*.begin=*/iree_hal_vulkan_direct_command_buffer_begin,
        /*.end=*/iree_hal_vulkan_direct_command_buffer_end,
        /*.begin_debug_group=*/
        iree_hal_vulkan_direct_command_buffer_begin_debug_group,
        /*.end_debug_group=*/
        iree_hal_vulkan_direct_command_buffer_end_debug_group,
        /*.execution_barrier=*/
        iree_hal_vulkan_direct_command_buffer_execution_barrier,
        /*.signal_event=*/
        iree_hal_vulkan_direct_command_buffer_signal_event,
        /*.reset_event=*/iree_hal_vulkan_direct_command_buffer_reset_event,
        /*.wait_events=*/iree_hal_vulkan_direct_command_buffer_wait_events,
        /*.discard_buffer=*/
        iree_hal_vulkan_direct_command_buffer_discard_buffer,
        /*.fill_buffer=*/iree_hal_vulkan_direct_command_buffer_fill_buffer,
        /*.update_buffer=*/
        iree_hal_vulkan_direct_command_buffer_update_buffer,
        /*.copy_buffer=*/iree_hal_vulkan_direct_command_buffer_copy_buffer,
        /*.collective=*/
        iree_hal_vulkan_direct_command_buffer_collective,
        /*.push_constants=*/
        iree_hal_vulkan_direct_command_buffer_push_constants,
        /*.push_descriptor_set=*/
        iree_hal_vulkan_direct_command_buffer_push_descriptor_set,
        /*.dispatch=*/iree_hal_vulkan_direct_command_buffer_dispatch,
        /*.dispatch_indirect=*/
        iree_hal_vulkan_direct_command_buffer_dispatch_indirect,
        /*.execute_commands=*/
        iree_hal_vulkan_direct_command_buffer_execute_commands,
};
}  // namespace
