//
// Created by Jesson on 2024/10/18.
//

#include <coro/awaitable/async_manual_reset_event.h>
#include <coro/types/task.h>
#include <coro/when_all_ready.h>
#include <coro/detail/sync_wait.h>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("async_manual_reset_event");

TEST_CASE("default constructor initially not set") {
    coro::async_manual_reset_event event;
    CHECK(!event.is_set());
}

TEST_CASE("construct event initially set") {
    coro::async_manual_reset_event event{ true };
    CHECK(event.is_set());
}

TEST_CASE("set and reset") {
    coro::async_manual_reset_event event;
    CHECK(!event.is_set());
    event.set();
    CHECK(event.is_set());
    event.set();
    CHECK(event.is_set());
    event.reset();
    CHECK(!event.is_set());
    event.reset();
    CHECK(!event.is_set());
    event.set();
    CHECK(event.is_set());
}

TEST_CASE("await not set event") {
    coro::async_manual_reset_event event;

    auto create_waiter = [&](bool& flag) -> coro::task<> {
        co_await event;
        flag = true;
    };

    bool completed1 = false;
    bool completed2 = false;

    auto check = [&]() -> coro::task<> {
        CHECK(!completed1);
        CHECK(!completed2);

        event.reset();

        CHECK(!completed1);
        CHECK(!completed2);

        event.set();

        CHECK(completed1);
        CHECK(completed2);

        co_return;
    };

    coro::sync_wait(coro::when_all_ready(
        create_waiter(completed1),
        create_waiter(completed2),
        check()));
}

TEST_CASE("awaiting already set event doesn't suspend") {
    coro::async_manual_reset_event event{ true };

    auto createWaiter = [&]() -> coro::task<> { co_await event; };

    // Should complete without blocking.
    coro::sync_wait(coro::when_all_ready(createWaiter(), createWaiter()));
}

TEST_SUITE_END();
