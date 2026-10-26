// Web/SDK boundary only. Emscripten 6.0.9's pthread_create failure branch
// unlinks the new pthread but does not free its stack/TLS allocation. Wait for
// that rollback to return before freeing a failed asynchronous creation.
#include <emscripten.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <emscripten/threading.h>

struct create_state { pthread_t failed_thread; };
static _Thread_local struct create_state *current_create;

EMSCRIPTEN_KEEPALIVE void *krkr2_pthread_create_state(void) {
    assert(current_create && "pthread_create must use the Web linker wrappers");
    return current_create;
}

EMSCRIPTEN_KEEPALIVE void krkr2_pthread_create_failed(
    struct create_state *state, pthread_t thread) {
    state->failed_thread = thread;
}

extern void _emscripten_thread_free_data(pthread_t thread);

typedef int (*create_fn)(pthread_t *, const pthread_attr_t *,
                         void *(*)(void *), void *);

static int create(create_fn original, pthread_t *thread,
                  const pthread_attr_t *attr, void *(*entry)(void *), void *arg) {
    struct create_state state = {0};
    struct create_state *previous = current_create;
    current_create = &state;
    int result = original(thread, attr, entry, arg);
    if (result && state.failed_thread) {
        _emscripten_thread_free_data(state.failed_thread);
    }
    current_create = previous;
    return result;
}

// libc++, C11 threads and the sanitizer interceptor can use different aliases.
// Nested wrappers are safe: only the innermost wrapper owns the failed block.
#define WRAP_CREATE(name)                                                       \
    extern int __real_##name(pthread_t *, const pthread_attr_t *,                \
                             void *(*)(void *), void *);                         \
    int __wrap_##name(pthread_t *thread, const pthread_attr_t *attr,              \
                      void *(*entry)(void *), void *arg) {                       \
        return create(__real_##name, thread, attr, entry, arg);                  \
    }

WRAP_CREATE(pthread_create)
WRAP_CREATE(__pthread_create)
WRAP_CREATE(emscripten_builtin_pthread_create)

// The SDK's browser futex loop already unmarks its wake address before
// _emscripten_yield and rechecks the atomic value afterwards. Yield the JS
// event loop at that existing boundary so asynchronous proxied I/O can finish
// while a worker owns the mutex. Keep the SDK's wake/timeout/cancellation code.
//
// Upstream the unmarked window lasts only for the queued-call pass, while an
// event-loop turn makes it long enough to drop a futex_wake that does not
// change the value. A wake aimed at the suspended address is therefore
// recorded here and reported through the SDK's existing yield exit, then
// returned as the ordinary wake result.
extern int __real_emscripten_futex_wait(volatile void *, uint32_t, double);
extern int __real_emscripten_futex_wake(volatile void *, int);
extern bool __real__emscripten_yield(double);
extern int krkr2_futex_may_suspend(void);
extern void krkr2_futex_yield(void);

static _Thread_local unsigned futex_depth;
static _Thread_local volatile void *futex_address;
static void *_Atomic suspended_futex;
static _Atomic bool suspended_woken;

struct futex_scope { unsigned depth; volatile void *address; };
static void leave_futex_scope(struct futex_scope *scope) {
    futex_depth = scope->depth;
    futex_address = scope->address;
}

int __wrap_emscripten_futex_wait(volatile void *address, uint32_t value,
                                double timeout) {
    if (!emscripten_is_main_browser_thread() || !krkr2_futex_may_suspend()) {
        return __real_emscripten_futex_wait(address, value, timeout);
    }
    struct futex_scope scope __attribute__((cleanup(leave_futex_scope))) = {
        futex_depth, futex_address
    };
    ++futex_depth;
    futex_address = address;
    int result = __real_emscripten_futex_wait(address, value, timeout);
    // Only this wait's own yield reports a recorded wake as an interruption.
    if (result == -EINTR && atomic_exchange(&suspended_woken, false)) {
        return 0;
    }
    return result;
}

int __wrap_emscripten_futex_wake(volatile void *address, int count) {
    if (!address || (((intptr_t)address) & 3) != 0 || count <= 0) {
        return __real_emscripten_futex_wake(address, count);
    }
    void *expected = (void *)address;
    if (!atomic_compare_exchange_strong(&suspended_futex, &expected, NULL)) {
        return __real_emscripten_futex_wake(address, count);
    }
    // Same accounting as the SDK's main-thread wake: INT_MAX wakes everyone
    // and reports only worker waiters.
    atomic_store(&suspended_woken, true);
    if (count == INT_MAX) {
        return __real_emscripten_futex_wake(address, count);
    }
    return count == 1 ? 1 : 1 + __real_emscripten_futex_wake(address, count - 1);
}

bool __wrap__emscripten_yield(double now) {
    if (futex_depth && emscripten_is_main_browser_thread() &&
        krkr2_futex_may_suspend()) {
        atomic_store(&suspended_woken, false);
        atomic_store(&suspended_futex, (void *)futex_address);
        krkr2_futex_yield();
        void *expected = (void *)futex_address;
        atomic_compare_exchange_strong(&suspended_futex, &expected, NULL);
        // The timestamp supplied before suspension is stale. Zero asks the
        // SDK timer implementation to obtain the current time if needed.
        bool timer_fired = __real__emscripten_yield(0);
        // A recorded wake leaves the SDK loop through its interruption exit.
        return timer_fired || atomic_load(&suspended_woken);
    }
    return __real__emscripten_yield(now);
}
