//
// Created by Jesson on 2024/10/18.
//

#include <coro/awaitable/async_auto_reset_event.h>
#include <coro/types/task.h>
#include <coro/when_all_ready.h>
#include <coro/when_all.h>
#include <coro/detail/sync_wait.h>
#include <coro/on_scope_exit.h>
#include <coro/thread_pool.h>

#include <thread>
#include <cassert>
#include <vector>
#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("async_auto_reset_event");

TEST_CASE("single waiter") {
    coro::async_auto_reset_event event;

    bool started = false;
    bool finished = false;
    auto run = [&]() -> coro::task<> {
        started = true;
        co_await event;
        finished = true;
    };

    auto check = [&]() -> coro::task<> {
        CHECK(started);
        CHECK(!finished);

        event.set();

        CHECK(finished);

        co_return;
    };

    coro::sync_wait(coro::when_all_ready(run(), check()));
}

TEST_CASE("multiple waiters") {
    coro::async_auto_reset_event event;

	
    auto run = [&](bool& flag) -> coro::task<> {
        co_await event;
        flag = true;
    };

    bool completed1 = false;
    bool completed2 = false;

    auto check = [&]() -> coro::task<> {
        CHECK(!completed1);
        CHECK(!completed2);

        event.set();

        CHECK(completed1);
        CHECK(!completed2);

        event.set();

        CHECK(completed2);

        co_return;
    };

    coro::sync_wait(coro::when_all_ready(
        run(completed1),
        run(completed2),
        check()));
}

TEST_CASE("multi-threaded") {
    coro::thread_pool tp{ 3 };

    auto run = [&]() -> coro::task<> {
        coro::async_auto_reset_event event;

        int value = 0;

        auto start_waiter = [&]() -> coro::task<> {
            co_await tp.schedule();
            co_await event;
            ++value;
            event.set();
        };

        auto start_signaller = [&]() -> coro::task<> {
            co_await tp.schedule();
            value = 5;
            event.set();
        };

        std::vector<coro::task<>> tasks;

        tasks.emplace_back(start_signaller());

        for (int i = 0; i < 1000; ++i) {
            tasks.emplace_back(start_waiter());
        }

        co_await when_all(std::move(tasks));

        // NOTE: Can't use CHECK() here because it's not thread-safe
        assert(value == 1005);
    };

    std::vector<coro::task<>> tasks;
    tasks.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        tasks.emplace_back(run());
    }

    sync_wait(when_all(std::move(tasks)));
}

TEST_SUITE_END();
