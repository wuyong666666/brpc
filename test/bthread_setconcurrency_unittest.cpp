// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// Author: Ge,Jun (gejun@baidu.com)
// Date: Sun Jul 13 15:04:18 CST 2014

#include <gtest/gtest.h>
#include "base/atomicops.h"
#include "base/time.h"
#include "base/macros.h"
#include "base/logging.h"
#include "base/thread_local.h"
#include <bthread/butex.h>
#include "base/logging.h"
#include "bthread/bthread.h"

namespace {
void* dummy(void*) {
    return NULL;
}

TEST(BthreadTest, setconcurrency) {
    ASSERT_EQ(8 + BTHREAD_EPOLL_THREAD_NUM, (size_t)bthread_getconcurrency());
    ASSERT_EQ(EINVAL, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY - 1));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(0));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(-1));
    ASSERT_EQ(EINVAL, bthread_setconcurrency(BTHREAD_MAX_CONCURRENCY + 1));
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 1));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 1, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY));  // smaller value
    bthread_t th;
    ASSERT_EQ(0, bthread_start_urgent(&th, NULL, dummy, NULL));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 1, bthread_getconcurrency());
    ASSERT_EQ(0, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 5));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 5, bthread_getconcurrency());
    ASSERT_EQ(EPERM, bthread_setconcurrency(BTHREAD_MIN_CONCURRENCY + 1));
    ASSERT_EQ(BTHREAD_MIN_CONCURRENCY + 5, bthread_getconcurrency());
}

static base::atomic<int> *odd;
static base::atomic<int> *even;

static base::atomic<int> nthreads(0);
static BAIDU_THREAD_LOCAL bool counted = false;
static base::atomic<bool> stop (false);

static void *odd_thread(void *) {
    while (!stop) {
        if (!counted) {
            counted = true;
            nthreads.fetch_add(1);
        }
        bthread::butex_wake_all(even);
        bthread::butex_wait(odd, 0, NULL);
    }
    return NULL;
}

static void *even_thread(void *) {
    while (!stop) {
        if (!counted) {
            counted = true;
            nthreads.fetch_add(1);
        }
        bthread::butex_wake_all(odd);
        bthread::butex_wait(even, 0, NULL);
    }
    return NULL;
}

TEST(BthreadTest, setconcurrency_with_running_bthread) {
    odd = bthread::butex_create_checked<base::atomic<int> >();
    even = bthread::butex_create_checked<base::atomic<int> >();
    ASSERT_TRUE(odd != NULL && even != NULL);
    *odd = 0;
    *even = 0;
    std::vector<bthread_t> tids;
    const int N = 700;
    for (int i = 0; i < N; ++i) {
        bthread_t tid;
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, odd_thread, NULL);
        tids.push_back(tid);
        bthread_start_background(&tid, &BTHREAD_ATTR_SMALL, even_thread, NULL);
        tids.push_back(tid);
    }
    for (int i = 100; i <= N; ++i) {
        ASSERT_EQ(0, bthread_setconcurrency(i));
        ASSERT_EQ(i, bthread_getconcurrency());
    }
    usleep(2000 * N);
    *odd = 1;
    *even = 1;
    stop =  true;
    bthread::butex_wake_all(odd);
    bthread::butex_wake_all(even);
    for (size_t i = 0; i < tids.size(); ++i) {
        bthread_join(tids[i], NULL);
    }
    LOG(INFO) << "All bthreads has quit";
    ASSERT_EQ(N, nthreads);
}
} // namespace