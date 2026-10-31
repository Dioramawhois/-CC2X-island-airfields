/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IAF_WASM_HOST_H
#define IAF_WASM_HOST_H
#include "iaf_module.h"
/* Single owner thread; no calls from loader-lock callbacks. A fault permanently closes
 * this host instance. The caller must abandon the current game callback on any error. */
int iaf_wasm_open(const char *path);
int iaf_wasm_ready(void);
const char *iaf_wasm_error(void);
void iaf_wasm_close(void);
int iaf_wasm_reset(void);
int iaf_wasm_create(uint32_t phase, uint32_t *handle);
int iaf_wasm_drop(uint32_t handle);
int iaf_wasm_dispatch(iaf_module_request *request, iaf_module_response *response);
#endif
