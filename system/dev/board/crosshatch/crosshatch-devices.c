// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>

#include <stdio.h>

#include "crosshatch.h"

static const pbus_dev_t dummy_display_dev = {
    .name = "dummy-display",
    .vid = PDEV_VID_GOOGLE,
    .did = PDEV_DID_CROSSHATCH_DISPLAY,
};

zx_status_t crosshatch_add_devices(crosshatch_t* crosshatch) {
    zx_status_t status;

    if ((status = pbus_device_add(&crosshatch->pbus, &dummy_display_dev)) != ZX_OK) {
        zxlogf(ERROR, "crosshatch_add_devices could not add dummy_display_dev: %d\n", status);
    }

    return ZX_OK;
}
