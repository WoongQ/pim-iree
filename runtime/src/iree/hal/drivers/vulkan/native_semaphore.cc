// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/hal/drivers/vulkan/native_semaphore.h"

#include <cstddef>

#include "iree/base/api.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/vulkan/dynamic_symbol_tables.h"
#include "iree/hal/drivers/vulkan/dynamic_symbols.h"
#include "iree/hal/drivers/vulkan/status_util.h"
#include "iree/hal/drivers/vulkan/util/ref_ptr.h"
#include "iree/hal/utils/semaphore_base.h"

// The maximum valid payload value of an iree_hal_semaphore_t.
// Payload values larger than this indicate that the semaphore has failed.
//
// This originates from Vulkan having a lower-bound of INT_MAX for
// maxTimelineSemaphoreValueDifference and many Android devices only supporting
// that lower-bound. At ~100 signals per second it'll take 1.5+ years to
// saturate. We may increase this value at some point but so long as there are
// some devices in the wild that may have this limitation we can ensure better
// consistency across the backends by observing this.
//
// The major mitigation here is that in proper usage of IREE there are no
// semaphores that are implicitly referenced by multiple VMs (each creates their
// own internally) and in a multitenant system each session should have its own
// semaphores - so even if the process lives for years it's highly unlikely any
// particular session does. Whatever, 640K is enough for anyone.
//
// See:
//   https://vulkan.gpuinfo.org/displayextensionproperty.php?name=maxTimelineSemaphoreValueDifference
#define IREE_HAL_VULKAN_SEMAPHORE_MAX_VALUE (2147483647ull - 1)

using namespace iree::hal::vulkan;

typedef struct iree_hal_vulkan_native_semaphore_t {
  iree_hal_semaphore_t base;
  
  iree_atomic_intptr_t failure_status;
} iree_hal_vulkan_native_semaphore_t;

namespace {
extern const iree_hal_semaphore_vtable_t
    iree_hal_vulkan_native_semaphore_vtable;
}  // namespace

static iree_hal_vulkan_native_semaphore_t*
iree_hal_vulkan_native_semaphore_cast(iree_hal_semaphore_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_native_semaphore_vtable);
  return (iree_hal_vulkan_native_semaphore_t*)base_value;
}

iree_status_t iree_hal_vulkan_native_semaphore_create(
    iree_allocator_t host_allocator, uint64_t initial_value,
    iree_hal_semaphore_t** out_semaphore) {
  
  IREE_ASSERT_ARGUMENT(out_semaphore);
  *out_semaphore = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_hal_vulkan_native_semaphore_t* semaphore = NULL;
  iree_status_t status = iree_allocator_malloc(
      host_allocator, sizeof(*semaphore), (void**)&semaphore);

  if (iree_status_is_ok(status)) {
    iree_hal_semaphore_initialize(&iree_hal_vulkan_native_semaphore_vtable,
                                  &semaphore->base);
    
    iree_atomic_store_intptr(&semaphore->failure_status, 0,
                             iree_memory_order_release);
    *out_semaphore = &semaphore->base;
  }

  IREE_TRACE_ZONE_END(z0);
  return status;
}

static void iree_hal_vulkan_native_semaphore_destroy(
    iree_hal_semaphore_t* base_semaphore) {
  iree_hal_vulkan_native_semaphore_t* semaphore =
      iree_hal_vulkan_native_semaphore_cast(base_semaphore);
  

  iree_hal_semaphore_deinitialize(&semaphore->base);
  
}

bool iree_hal_vulkan_native_semaphore_isa(iree_hal_semaphore_t* semaphore) {
  return iree_hal_resource_is(&semaphore->resource,
                              &iree_hal_vulkan_native_semaphore_vtable);
}

static iree_status_t iree_hal_vulkan_native_semaphore_query(
    iree_hal_semaphore_t* base_semaphore, uint64_t* out_value) {
  iree_hal_vulkan_native_semaphore_t* semaphore =
      iree_hal_vulkan_native_semaphore_cast(base_semaphore);
  
  (void)semaphore;

  //printf("\n[semaphore query] do nothing!\n");

  return iree_ok_status();
}

static iree_status_t iree_hal_vulkan_native_semaphore_signal(
    iree_hal_semaphore_t* base_semaphore, uint64_t new_value) {
  iree_hal_vulkan_native_semaphore_t* semaphore =
      iree_hal_vulkan_native_semaphore_cast(base_semaphore);

  (void)semaphore;

  printf("\n[semaphore signal] do nothing!\n");

  return iree_ok_status();
}

static void iree_hal_vulkan_native_semaphore_fail(
    iree_hal_semaphore_t* base_semaphore, iree_status_t status) {
  iree_hal_vulkan_native_semaphore_t* semaphore =
      iree_hal_vulkan_native_semaphore_cast(base_semaphore);
  
  (void)semaphore;

  printf("\n[semaphore fail] do nothing!\n");
}


static iree_status_t iree_hal_vulkan_native_semaphore_wait(
    iree_hal_semaphore_t* base_semaphore, uint64_t value,
    iree_timeout_t timeout) {
  iree_hal_vulkan_native_semaphore_t* semaphore =
      iree_hal_vulkan_native_semaphore_cast(base_semaphore);
  
  (void)semaphore;

  return iree_ok_status();
}

namespace {
const iree_hal_semaphore_vtable_t iree_hal_vulkan_native_semaphore_vtable = {
    /*.destroy=*/iree_hal_vulkan_native_semaphore_destroy,
    /*.query=*/iree_hal_vulkan_native_semaphore_query,
    /*.signal=*/iree_hal_vulkan_native_semaphore_signal,
    /*.fail=*/iree_hal_vulkan_native_semaphore_fail,
    /*.wait=*/iree_hal_vulkan_native_semaphore_wait,
};
}  // namespace
