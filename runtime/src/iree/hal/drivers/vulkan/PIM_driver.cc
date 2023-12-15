// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/PIM_driver.h"

#include <cstdint>
#include <cstring>

#include "iree/base/api.h"
#include "iree/base/tracing.h"
#include "iree/hal/api.h"
#include "iree/hal/drivers/vulkan/api.h"
#include "iree/hal/drivers/vulkan/dynamic_symbols.h"
#include "iree/hal/drivers/vulkan/status_util.h"
#include "iree/hal/drivers/vulkan/util/arena.h"
#include "iree/hal/drivers/vulkan/util/ref_ptr.h"
#include "iree/hal/drivers/vulkan/PIM_device.h"

using namespace iree::hal::vulkan;

typedef struct iree_hal_vulkan_driver_t {
  iree_hal_resource_t resource;
  iree_allocator_t host_allocator;

  // Identifier used for the driver in the IREE driver registry.
  // We allow overriding so that multiple Vulkan versions can be exposed in the
  // same process.
  iree_string_view_t identifier;

  iree_hal_vulkan_device_options_t device_options;
  
} iree_hal_vulkan_driver_t;

namespace {
extern const iree_hal_driver_vtable_t iree_hal_vulkan_driver_vtable;
}  // namespace

static iree_hal_vulkan_driver_t* iree_hal_vulkan_driver_cast(
    iree_hal_driver_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_driver_vtable);
  return (iree_hal_vulkan_driver_t*)base_value;
}

IREE_API_EXPORT void iree_hal_vulkan_driver_options_initialize(
    iree_hal_vulkan_driver_options_t* out_options) {
  memset(out_options, 0, sizeof(*out_options));
  out_options->api_version = VK_API_VERSION_1_2;
  out_options->requested_features = 0;
  out_options->debug_verbosity = 0;
  iree_hal_vulkan_device_options_initialize(&out_options->device_options);
}

// NOTE: takes ownership of |instance|.
static iree_status_t iree_hal_vulkan_driver_create_internal(
    iree_string_view_t identifier,
    const iree_hal_vulkan_driver_options_t* options,
    iree_allocator_t host_allocator,
    iree_hal_driver_t** out_driver) {
  
  iree_hal_vulkan_driver_t* driver = NULL;
  iree_host_size_t total_size = sizeof(*driver) + identifier.size;
  iree_status_t status =
      iree_allocator_malloc(host_allocator, total_size, (void**)&driver);
  
  iree_hal_resource_initialize(&iree_hal_vulkan_driver_vtable,
                               &driver->resource);
  driver->host_allocator = host_allocator;
  iree_string_view_append_to_buffer(
      identifier, &driver->identifier,
      (char*)driver + total_size - identifier.size);

  memcpy(&driver->device_options, &options->device_options,
         sizeof(driver->device_options));
   
  *out_driver = (iree_hal_driver_t*)driver;
  return status;
}

static void iree_hal_vulkan_driver_destroy(iree_hal_driver_t* base_driver) {
  iree_hal_vulkan_driver_t* driver = iree_hal_vulkan_driver_cast(base_driver);
  iree_allocator_t host_allocator = driver->host_allocator;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_allocator_free(host_allocator, driver);

  IREE_TRACE_ZONE_END(z0);
}

IREE_API_EXPORT iree_status_t iree_hal_vulkan_driver_create(
    iree_string_view_t identifier,
    const iree_hal_vulkan_driver_options_t* options,
    iree_allocator_t host_allocator,
    iree_hal_driver_t** out_driver) {
  IREE_ASSERT_ARGUMENT(options);
  IREE_ASSERT_ARGUMENT(out_driver);
  IREE_TRACE_SCOPE();

  
  iree_status_t status = iree_ok_status();

  if (iree_status_is_ok(status)) {
    status = iree_hal_vulkan_driver_create_internal(
        identifier, options, host_allocator, out_driver);
  }

  if (!iree_status_is_ok(status)) {
    
  }
  return status;
}

static iree_status_t iree_hal_vulkan_driver_query_available_devices(
    iree_hal_driver_t* base_driver, iree_allocator_t host_allocator,
    iree_host_size_t* out_device_info_count,
    iree_hal_device_info_t** out_device_infos) {
  iree_hal_vulkan_driver_t* driver = iree_hal_vulkan_driver_cast(base_driver);

  (void)driver;

  printf("\nquery devices do nothing\n\n");

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_driver_dump_device_info(
    iree_hal_driver_t* base_driver, iree_hal_device_id_t device_id,
    iree_string_builder_t* builder) {
  iree_hal_vulkan_driver_t* driver = iree_hal_vulkan_driver_cast(base_driver);
  // TODO(benvanik): dump detailed device info.
  (void)driver;
  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_driver_create_device_by_id(
    iree_hal_driver_t* base_driver, iree_hal_device_id_t device_id,
    iree_host_size_t param_count, const iree_string_pair_t* params,
    iree_allocator_t host_allocator, iree_hal_device_t** out_device) {
  iree_hal_vulkan_driver_t* driver = iree_hal_vulkan_driver_cast(base_driver);
  IREE_TRACE_ZONE_BEGIN(z0);

    
  // iree_string_view_t device_name =
  //     iree_make_string_view(physical_device_properties.deviceName,
  //                           strlen(physical_device_properties.deviceName));
  iree_string_view_t device_name = iree_make_cstring_view("PIM");

  // Attempt to create the device.
  // This may fail if the device was enumerated but is in exclusive use,
  // disabled by the system, or permission is denied.
  iree_status_t status = iree_hal_vulkan_device_create(
      base_driver, device_name, &driver->device_options, host_allocator, out_device);

  IREE_TRACE_ZONE_END(z0);
  return status;
}

static iree_status_t iree_hal_vulkan_driver_create_device_by_path(
    iree_hal_driver_t* base_driver, iree_string_view_t driver_name,
    iree_string_view_t device_path, iree_host_size_t param_count,
    const iree_string_pair_t* params, iree_allocator_t host_allocator,
    iree_hal_device_t** out_device) {
  if (iree_string_view_is_empty(device_path)) {
    return iree_hal_vulkan_driver_create_device_by_id(
        base_driver, IREE_HAL_DEVICE_ID_DEFAULT, param_count, params,
        host_allocator, out_device);
  }

  return iree_make_status(IREE_STATUS_UNIMPLEMENTED, "unsupported device path");
}

namespace {
const iree_hal_driver_vtable_t iree_hal_vulkan_driver_vtable = {
    /*.destroy=*/iree_hal_vulkan_driver_destroy,
    /*.query_available_devices=*/
    iree_hal_vulkan_driver_query_available_devices,
    /*.dump_device_info=*/iree_hal_vulkan_driver_dump_device_info,
    /*.create_device_by_id=*/iree_hal_vulkan_driver_create_device_by_id,
    /*.create_device_by_path=*/iree_hal_vulkan_driver_create_device_by_path,
};
}  // namespace
