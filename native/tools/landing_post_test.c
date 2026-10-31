/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ownership/ABI tests of production landing_post.h plus production SDK session_post.h.
 * No world model: handler is a spy; these tests do not prove landing/game authority.
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned allocations, releases;
static int fail_allocation;
static void *tracked_alloc(size_t size) {
    if (fail_allocation) return NULL;
    void *p = malloc(size);
    if (p) ++allocations;
    return p;
}
static void tracked_free(void *p) {
    assert(p);
    ++releases;
    free(p);
}
#define malloc tracked_alloc
#define free tracked_free
#include "../plugin/landing_post.h"
#undef malloc
#undef free
#include "session_lifecycle.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock(void *unused) { (void)unused; assert(!pthread_mutex_lock(&mutex)); }
static void unlock(void *unused) { (void)unused; assert(!pthread_mutex_unlock(&mutex)); }
static SessionPosts queue = {.lock = lock, .unlock = unlock};
static int server_object, other_server, calls;
static void *current_server = &server_object;
static void *server(void) { return current_server; }
static uint64_t token(void) { return session_posts_token(&queue); }
static cc2x_post_result post(uint64_t t, void (*run)(void *, void *), void (*cancel)(void *), void *user) {
    return session_posts_push(&queue, t, run, cancel, user);
}
static void handler(uint32_t vehicle, uint32_t tile, uint32_t team) {
    assert(vehicle == UINT32_MAX && tile == 0x80000001u && team == 12);
    ++calls;
}
static cc2x_api api = {
    .api_version = 5, .size = sizeof(cc2x_api), .server = server,
    .session_token = token, .server_post_session = post
};
static const char *submit(uint64_t t) {
    return iaf_landing_submit(&api, t, UINT32_MAX, 0x80000001u, 12, handler);
}
static cc2x_post_result forced;
static cc2x_post_result reject(uint64_t t, void (*run)(void *, void *), void (*cancel)(void *), void *user) {
    (void)t; (void)run; (void)cancel; (void)user;
    return forced;
}
static cc2x_post_result close_during_enqueue(uint64_t t, void (*run)(void *, void *),
        void (*cancel)(void *), void *user) {
    session_posts_close(&queue);
    return post(t, run, cancel, user);
}

int main(void) {
    _Static_assert(sizeof(((IafLandingPost *)0)->vehicle) == 4, "uint32 IDs");
    _Static_assert(sizeof(((IafLandingPost *)0)->token) == 8, "uint64 token");
    /* Size/version gating before any tail pointer dereference. */
    api.api_version = 4;
    assert(!iaf_session_api(&api));
    assert(!strcmp(submit(1), "api5_required"));
    api.api_version = 5;
    api.size = offsetof(cc2x_api, server_post_session);
    assert(!iaf_session_api(&api));
    api.size = sizeof(api);
    api.server_post_session = NULL;
    assert(!iaf_session_api(&api));
    api.server_post_session = post;
    assert(iaf_session_api(&api));
    assert(!strcmp(submit(0), "session_required"));
    assert(!strcmp(submit(1), "session_closed"));
    session_posts_begin(&queue);
    uint64_t old = token();
    fail_allocation = 1;
    assert(!strcmp(submit(old), "out_of_memory"));
    fail_allocation = 0;
    assert(!strcmp(iaf_landing_submit(&api, old, 0, 1, 12, handler), "invalid_request"));
    assert(!submit(old));
    session_posts_drain(&queue, &server_object);
    assert(calls == 1);
    assert(!submit(old));
    session_posts_close(&queue); /* actual cancellation frees payload; sanitizers check it */
    assert(calls == 1);
    session_posts_begin(&queue);
    assert(!strcmp(submit(old), "stale_session"));
    session_posts_drain(&queue, &server_object);
    assert(calls == 1);

    assert(!submit(token()));
    current_server = &other_server;
    session_posts_drain(&queue, &server_object); /* server mismatch: release without handler */
    assert(calls == 1);
    current_server = &server_object;
    for (unsigned i = 0; i < SESSION_POST_MAX; ++i) assert(!submit(token()));
    assert(!strcmp(submit(token()), "queue_full"));
    session_posts_close(&queue); /* full queue cancellation owns/frees each payload once */
    assert(calls == 1);
    session_posts_begin(&queue);

    api.server_post_session = reject;
    const cc2x_post_result results[] = {CC2X_POST_INVALID, CC2X_POST_CLOSED, CC2X_POST_STALE, CC2X_POST_FULL};
    const char *reasons[] = {"invalid_request", "session_closed", "stale_session", "queue_full"};
    for (unsigned i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        forced = results[i];
        assert(!strcmp(submit(token()), reasons[i])); /* negative != success; caller frees */
    }
    api.server_post_session = close_during_enqueue;
    assert(!strcmp(submit(token()), "session_closed")); /* precheck is not acceptance */
    api.server_post_session = post;
    session_posts_begin(&queue);
    old = token();
    assert(!submit(old));
    session_posts_begin(&queue); /* replacement, including same server address */
    session_posts_drain(&queue, &server_object);
    assert(calls == 1 && token() != old);
    assert(!submit(token()));
    session_posts_drain(&queue, &server_object);
    assert(calls == 2);
    session_posts_close(&queue);

    /* Actual hook admission seam: same-address init without end cancels the IAF
     * heap payload and refuses the next UI request without allocating anything. */
    SessionLifecycle lifecycle = {0};
    session_lifecycle_init_enter(&lifecycle, &queue, &server_object);
    session_lifecycle_init_leave(&lifecycle, &queue);
    old = token();
    assert(old && !submit(old));
    unsigned before_cancel = releases;
    session_lifecycle_init_enter(&lifecycle, &queue, &server_object);
    assert(releases == before_cancel + 1);
    session_lifecycle_init_leave(&lifecycle, &queue);
    unsigned before_reject = allocations;
    assert(!token() && !strcmp(submit(old), "session_closed"));
    assert(allocations == before_reject);
    session_lifecycle_tick(&lifecycle, &queue, &server_object);
    session_posts_drain(&queue, &server_object);
    assert(calls == 2);
    session_lifecycle_end(&lifecycle, &queue, &server_object);
    session_lifecycle_init_enter(&lifecycle, &queue, &server_object);
    session_lifecycle_init_leave(&lifecycle, &queue);
    assert(token() && token() != old);
    assert(!strcmp(submit(old), "stale_session"));
    assert(!submit(token()));
    session_posts_drain(&queue, &server_object);
    assert(calls == 3);
    session_lifecycle_end(&lifecycle, &queue, &server_object);
    assert(allocations == releases && allocations > SESSION_POST_MAX);
    printf("IAF payload allocations=%u releases=%u\n", allocations, releases);
    puts("PASS IAF production landing ownership + SDK queue: ABI gating, uint32 IDs, execution, "
         "cancellation, stale/replaced/closed/full/invalid, server mismatch, negative rejection");
    return 0;
}
