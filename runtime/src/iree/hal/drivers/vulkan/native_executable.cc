// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/native_executable.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "iree/base/api.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/vulkan/dynamic_symbol_tables.h"
#include "iree/hal/drivers/vulkan/dynamic_symbols.h"
#include "iree/hal/drivers/vulkan/handle_util.h"
#include "iree/hal/drivers/vulkan/native_pipeline_layout.h"
#include "iree/hal/drivers/vulkan/status_util.h"
#include "iree/hal/drivers/vulkan/util/ref_ptr.h"

// flatcc schemas:
#include "iree/base/internal/flatcc/parsing.h"
#include "iree/schemas/pim_executable_def_reader.h"
#include "iree/schemas/pim_executable_def_verifier.h"

using namespace iree::hal::vulkan;

typedef struct iree_hal_vulkan_entry_point_t {
  
  iree_string_view_t name;
} iree_hal_vulkan_entry_point_t;



// Verifies the structure of the FlatBuffer so that we can avoid doing so during
// runtime. There are still some conditions we must be aware of (such as omitted
// names on functions with internal linkage), however we shouldn't need to
// bounds check anything within the FlatBuffer after this succeeds.
static iree_status_t iree_hal_pim_executable_flatbuffer_verify(
    iree_const_byte_span_t flatbuffer_data,
    iree_host_size_t expected_entry_point_count) {
  if (!flatbuffer_data.data || flatbuffer_data.data_length < 16) {
    return iree_make_status(
        IREE_STATUS_INVALID_ARGUMENT,
        "FlatBuffer data is not present or less than 16 bytes (%zu total)",
        flatbuffer_data.data_length);
  }

  // Run flatcc generated verification. This ensures all pointers are in-bounds
  // and that we can safely walk the file, but not that the actual contents of
  // the FlatBuffer meet our expectations.
  int verify_ret = iree_PIMExecutableDef_verify_as_root(
      flatbuffer_data.data, flatbuffer_data.data_length);
  if (verify_ret != flatcc_verify_ok) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "FlatBuffer verification failed: %s",
                            flatcc_verify_error_string(verify_ret));
  }

  iree_PIMExecutableDef_table_t executable_def =
      iree_PIMExecutableDef_as_root(flatbuffer_data.data);

  flatbuffers_string_vec_t entry_points_vec =
      iree_PIMExecutableDef_entry_points_get(executable_def);
  size_t entry_point_count = flatbuffers_string_vec_len(entry_points_vec);
  if (entry_point_count != expected_entry_point_count) {
    return iree_make_status(IREE_STATUS_FAILED_PRECONDITION,
                            "executable provides %zu entry points but caller "
                            "provided %zu; must match",
                            entry_point_count, expected_entry_point_count);
  }

  for (size_t i = 0; i < entry_point_count; ++i) {
    if (!flatbuffers_string_len(
            flatbuffers_string_vec_at(entry_points_vec, i))) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "executable entry point %zu has no name", i);
    }
  }

  /*
  flatbuffers_uint32_vec_t subgroup_sizes_vec =
      iree_pimExecutableDef_subgroup_sizes_get(executable_def);
  if (subgroup_sizes_vec) {
    size_t subgroup_sizes_count = flatbuffers_vec_len(subgroup_sizes_vec);
    if (subgroup_sizes_count != expected_entry_point_count) {
      return iree_make_status(
          IREE_STATUS_INVALID_ARGUMENT,
          "executable has %zu entry points but %zu subgroup sizes are defined",
          expected_entry_point_count, subgroup_sizes_count);
    }
  }*/

  if (flatbuffers_uint64_vec_len(
          iree_PIMExecutableDef_code_get(executable_def)) == 0) {
    //return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
    //                        "executable SPIR-V code is missing/empty");
  }

  return iree_ok_status();
}

typedef struct iree_hal_vulkan_native_executable_t {
  iree_hal_resource_t resource;
  
  flatbuffers_uint64_vec_t code_vec;

  int cmd_stack_len;

  iree_host_size_t entry_point_count;
  iree_hal_vulkan_entry_point_t entry_points[];
} iree_hal_vulkan_native_executable_t;

namespace {
extern const iree_hal_executable_vtable_t
    iree_hal_vulkan_native_executable_vtable;
}  // namespace

static iree_hal_vulkan_native_executable_t*
iree_hal_vulkan_native_executable_cast(iree_hal_executable_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_native_executable_vtable);
  return (iree_hal_vulkan_native_executable_t*)base_value;
}

iree_status_t iree_hal_vulkan_native_executable_create(
    iree_allocator_t host_allocator,
    const iree_hal_executable_params_t* executable_params,
    iree_hal_executable_t** out_executable) {
  
  IREE_ASSERT_ARGUMENT(executable_params);
  IREE_ASSERT_ARGUMENT(out_executable);
  *out_executable = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);

  // Verify and fetch the executable FlatBuffer wrapper.
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_hal_pim_executable_flatbuffer_verify(
              executable_params->executable_data,
              executable_params->pipeline_layout_count));
  iree_PIMExecutableDef_table_t executable_def =
      iree_PIMExecutableDef_as_root(executable_params->executable_data.data);

  // decode pim code
  flatbuffers_uint64_vec_t code_vec =
      iree_PIMExecutableDef_code_get(executable_def);
  

  int cmd_len =
      flatbuffers_uint64_vec_len(code_vec);

  // Create pipelines for each entry point.
  flatbuffers_string_vec_t entry_points_vec =
      iree_PIMExecutableDef_entry_points_get(executable_def);
  
  iree_host_size_t entry_point_count =
      flatbuffers_string_vec_len(entry_points_vec);

  iree_hal_vulkan_native_executable_t* executable = NULL;
  iree_host_size_t total_size =
      sizeof(*executable) +
      entry_point_count * sizeof(*executable->entry_points);
  iree_status_t status = iree_allocator_malloc(host_allocator,
                                               total_size, (void**)&executable);
  if (iree_status_is_ok(status)) {
    iree_hal_resource_initialize(&iree_hal_vulkan_native_executable_vtable,
                                 &executable->resource);
    
    executable->entry_point_count = entry_point_count;
    executable->code_vec = code_vec;
    
    executable->cmd_stack_len = cmd_len;

    memset(executable->entry_points, 0,
           entry_point_count * sizeof(*executable->entry_points));
  }
  
  
  if (iree_status_is_ok(status)) {
    *out_executable = (iree_hal_executable_t*)executable;
  } else {
    iree_hal_executable_destroy((iree_hal_executable_t*)executable);
  }

  IREE_TRACE_ZONE_END(z0);
  return status;
}

static void iree_hal_vulkan_native_executable_destroy(
    iree_hal_executable_t* base_executable) {
  iree_hal_vulkan_native_executable_t* executable =
      iree_hal_vulkan_native_executable_cast(base_executable);
 
  IREE_TRACE_ZONE_BEGIN(z0);

  (void)executable;

  IREE_TRACE_ZONE_END(z0);
}

flatbuffers_uint64_vec_t iree_hal_pim_executable_cmd_get(iree_hal_executable_t* base_executable){
  iree_hal_vulkan_native_executable_t* executable =
      iree_hal_vulkan_native_executable_cast(base_executable);

  return executable->code_vec;
}

int iree_hal_pim_executable_cmd_len(iree_hal_executable_t* base_executable){
  iree_hal_vulkan_native_executable_t* executable =
      iree_hal_vulkan_native_executable_cast(base_executable);

  return executable->cmd_stack_len;
}

namespace {
const iree_hal_executable_vtable_t iree_hal_vulkan_native_executable_vtable = {
    /*.destroy=*/iree_hal_vulkan_native_executable_destroy,
};
}  // namespace
