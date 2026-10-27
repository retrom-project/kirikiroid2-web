// Execute the actual bridge with controlled worker preemption. The small SDK
// stubs model its yield interruption and recursive proxy-queue mutex wait;
// the real SDK and JSPI are exercised separately by pthread_jspi_smoke.cpp.
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <pthread.h>

static void after_wake_claim(bool success);
#undef atomic_compare_exchange_strong
#define atomic_compare_exchange_strong(ptr, expected, desired)                  \
    __extension__ ({                                                           \
        __auto_type test_ptr = (ptr);                                          \
        bool test_ok = __atomic_compare_exchange_n(                            \
            test_ptr, (expected), (desired), 0,                                \
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);                                \
        after_wake_claim(test_ok);                                             \
        test_ok;                                                              \
    })
#include "../../../platforms/web/pthread_jspi.c"

static _Thread_local bool browser_thread = true;
static _Atomic int phase;
static bool publication_test, in_nested;
static int outer_value, inner_value, wake_result, inner_result;
static unsigned yields;

static void wait_phase(int target) {
    while (atomic_load(&phase) < target) sched_yield();
}

static void after_wake_claim(bool success) {
    if (publication_test && !browser_thread && success) {
        atomic_store(&phase, 2);
        wait_phase(3);
    }
}

bool emscripten_is_main_browser_thread(void) { return browser_thread; }
int krkr2_futex_may_suspend(void) { return 1; }

int __real_emscripten_futex_wake(volatile void *address, int count) {
    (void)address;
    (void)count;
    return 0; // No additional worker waiters.
}

void krkr2_futex_yield(void) {
    ++yields;
    if (publication_test && yields == 1) {
        atomic_store(&phase, 1);
        wait_phase(2);
    } else if (!publication_test && !in_nested) {
        browser_thread = false;
        wake_result = __wrap_emscripten_futex_wake(&outer_value, 1);
        browser_thread = true;
    }
}

bool __real__emscripten_yield(double now) {
    (void)now;
    if (!publication_test && !in_nested) {
        in_nested = true;
        // The SDK queued-call pass can contend on its proxy queue mutex,
        // entering another futex wait before the outer yield returns.
        inner_result = __wrap_emscripten_futex_wait(&inner_value, 0, 2);
        in_nested = false;
    }
    return false;
}

int __real_emscripten_futex_wait(volatile void *address, uint32_t value,
                                double timeout) {
    (void)address;
    (void)value;
    (void)timeout;
    int iterations = publication_test ? 2 : 1;
    for (int i = 0; i < iterations; ++i) {
        bool interrupted = __wrap__emscripten_yield(0);
        if (publication_test && i == 0) {
            // Resume the worker after the browser has consumed the claimed
            // wake, but before the next yield can overwrite its notification.
            atomic_store(&phase, 3);
            wait_phase(4);
        }
        if (interrupted) return -EINTR;
    }
    // The futex value did not change. Without a notification, the SDK
    // continues waiting until its timeout (or forever for an infinite wait).
    return -ETIMEDOUT;
}

static void *wake_worker(void *unused) {
    (void)unused;
    browser_thread = false;
    wait_phase(1);
    wake_result = __wrap_emscripten_futex_wake(&outer_value, 1);
    atomic_store(&phase, 4);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    publication_test = strcmp(argv[1], "publication") == 0;
    pthread_t worker;
    if (publication_test && pthread_create(&worker, NULL, wake_worker, NULL)) {
        return 2;
    }
    int result = __wrap_emscripten_futex_wait(&outer_value, 0, 250);
    if (publication_test) pthread_join(worker, NULL);
    printf("%s: wake=%d wait=%d value=%d nested=%d\n",
           argv[1], wake_result, result, outer_value, inner_result);
    return wake_result == 1 && result == 0 && outer_value == 0 &&
           (publication_test || inner_result == -ETIMEDOUT) ? 0 : 1;
}
