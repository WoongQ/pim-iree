// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_HAL_DRIVERS_VULKAN_NATIVE_EXECUTABLE_H_
#define IREE_HAL_DRIVERS_VULKAN_NATIVE_EXECUTABLE_H_

// clang-format off: must be included before all other headers.
#include "iree/hal/drivers/vulkan/vulkan_headers.h"
// clang-format on

#include "iree/base/api.h"
#include "iree/hal/api.h"
#include "iree/hal/drivers/vulkan/handle_util.h"

// flatcc schemas:
#include "iree/base/internal/flatcc/parsing.h"
#include "iree/schemas/pim_executable_def_reader.h"
#include "iree/schemas/pim_executable_def_verifier.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct iree_hal_vulkan_source_location_t {
  iree_string_view_t file_name;
  int line;
  iree_string_view_t func_name;
} iree_hal_vulkan_source_location_t;

// Creates a wrapper for one or more VkPipelines that are sourced from the same
// IREE executable. Each of the pipelines will share the same shader module
// and just differs by the entry point into the shader module they reference.
iree_status_t iree_hal_vulkan_native_executable_create(
    iree_allocator_t host_allocator,
    const iree_hal_executable_params_t* executable_params,
    iree_hal_executable_t** out_executable);

// Returns the source location for the given entry point. May be empty if not
// available.
void iree_hal_vulkan_native_executable_entry_point_source_location(
    iree_hal_executable_t* executable, iree_host_size_t entry_ordinal,
    iree_hal_vulkan_source_location_t* out_source_location);

// Returns the cached VkPipeline for the given executable |entry_ordinal|.
iree_status_t iree_hal_vulkan_native_executable_pipeline_for_entry_point(
    iree_hal_executable_t* executable, iree_host_size_t entry_ordinal,
    VkPipeline* out_pipeline_handle);

flatbuffers_uint64_vec_t iree_hal_pim_executable_cmd_get(iree_hal_executable_t* base_executable);

int iree_hal_pim_executable_cmd_len(iree_hal_executable_t* base_executable);



#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_HAL_DRIVERS_VULKAN_NATIVE_EXECUTABLE_H_
