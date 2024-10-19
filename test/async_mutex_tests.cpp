//
// Created by Jesson on 2024/10/18.
//

#include <coro/types/task.h>
#include <coro/when_all_ready.h>
#include <coro/detail/sync_wait.h>
#include <coro/awaitable/single_consumer_event.h>
#include <coro/awaitable/async_mutex.h>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("async_mutex");

TEST_CASE("try_lock") {
    coro::async_mutex mutex;
    CHECK(mutex.try_lock());
    CHECK_FALSE(mutex.try_lock());
    mutex.unlock();
    CHECK(mutex.try_lock());
}

TEST_CASE("multiple lockers") {
    int value = 0;
    coro::async_mutex mutex;
    coro::single_consumer_event a;
    coro::single_consumer_event b;
    coro::single_consumer_event c;
    coro::single_consumer_event d;

    auto f = [&](coro::single_consumer_event& e) -> coro::task<>
    {
        auto lock = co_await mutex.scoped_lock_async();
        co_await e;
        ++value;
    };

    auto check = [&]() -> coro::task<>
    {
        CHECK(value == 0);

        a.set();

        CHECK(value == 1);

        auto check2 = [&]() -> coro::task<>
        {
            b.set();

            CHECK(value == 2);

            c.set();

            CHECK(value == 3);

            d.set();

            CHECK(value == 4);

            co_return;
        };

        // Now that we've queued some waiters and released one waiter this will
        // have acquired the list of pending waiters in the local cache.
        // We'll now queue up another one before releasing any more waiters
        // to test the code-path that looks at the newly queued waiter list
        // when the cache of waiters is exhausted.
        (void)co_await coro::when_all_ready(f(d), check2());
    };

    coro::sync_wait(coro::when_all_ready(
        f(a),
        f(b),
        f(c),
        check()));

    CHECK(value == 4);
}

TEST_SUITE_END();
