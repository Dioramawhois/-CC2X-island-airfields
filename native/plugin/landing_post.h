/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual landing submission ownership path, shared with portable native tests.
 * No game objects may be captured here. The handler resolves IDs on the logic thread.
 */
#ifndef IAF_LANDING_POST_H
#define IAF_LANDING_POST_H
#include <stdlib.h>
#include "cc2x.h"

typedef void (*IafLandingHandler)(uint32_t vehicle, uint32_t tile, uint32_t team);
typedef struct {
    const cc2x_api *api;
    IafLandingHandler handler;
    uint64_t token;
    uint32_t vehicle, tile, team;
} IafLandingPost;

static inline int iaf_session_api(const cc2x_api *api) {
    return api && api->api_version >= 5 &&
        api->size >= offsetof(cc2x_api, server_post_session) + sizeof(api->server_post_session) &&
        api->session_token && api->server_post_session;
}
static inline void iaf_landing_cancel(void *user) {
    /* No world, API calls, server pointers or plugin state accessed on cancellation. */
    free(user);
}
static inline void iaf_landing_run(void *server, void *user) {
    IafLandingPost *p = user;
    if (server && p->api->server() == server && p->api->session_token() == p->token)
        p->handler(p->vehicle, p->tile, p->team);
    free(p);
}
/* NULL = accepted; a reason = rejected, with no ownership left outstanding. */
static inline const char *iaf_landing_submit(const cc2x_api *api, uint64_t token,
        uint32_t vehicle, uint32_t tile, uint32_t team, IafLandingHandler handler) {
    if (!iaf_session_api(api)) return "api5_required";
    if (!token) return "session_required";
    if (!vehicle || !tile || !handler) return "invalid_request";
    uint64_t current = api->session_token();
    if (!current) return "session_closed";
    if (token != current) return "stale_session";
    IafLandingPost *p = malloc(sizeof(*p));
    if (!p) return "out_of_memory";
    *p = (IafLandingPost){api, handler, token, vehicle, tile, team};
    cc2x_post_result result = api->server_post_session(token, iaf_landing_run, iaf_landing_cancel, p);
    if (result == CC2X_POST_ACCEPTED) return NULL;
    free(p); /* rejection never transfers ownership, including negative enum values */
    switch (result) {
    case CC2X_POST_CLOSED: return "session_closed";
    case CC2X_POST_STALE: return "stale_session";
    case CC2X_POST_FULL: return "queue_full";
    default: return "invalid_request";
    }
}
#endif
