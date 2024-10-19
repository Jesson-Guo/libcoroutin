//
// Created by Jesson on 2024/10/19.
//

#include <coro/awaitable/async_latch.h>
#include <coro/types/task.h>
#include <coro/when_all_ready.h>
#include <coro/detail/sync_wait.h>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("async_latch");

TEST_CASE("latch constructed with zero count is initially ready") {
    coro::async_latch latch(0);
    CHECK(latch.is_ready());
}

TEST_CASE("latch constructed with negative count is initially ready") {
    coro::async_latch latch(-3);
    CHECK(latch.is_ready());
}

TEST_CASE("count_down and is_ready") {
    coro::async_latch latch(3);
    CHECK(!latch.is_ready());
    latch.count_down();
    CHECK(!latch.is_ready());
    latch.count_down();
    CHECK(!latch.is_ready());
    latch.count_down();
    CHECK(latch.is_ready());
}

TEST_CASE("count_down by n") {
    coro::async_latch latch(5);
    latch.count_down(3);
    CHECK(!latch.is_ready());
    latch.count_down(2);
    CHECK(latch.is_ready());
}

TEST_CASE("single awaiter") {
    coro::async_latch latch(2);
    bool after = false;
    coro::sync_wait(coro::when_all_ready(
        [&]() -> coro::task<> {
            co_await latch;
            after = true;
        }(),
        [&]() -> coro::task<> {
            CHECK(!after);
            latch.count_down();
            CHECK(!after);
            latch.count_down();
            CHECK(after);
            co_return;
        }()
    ));
}

TEST_CASE("multiple awaiters") {
    coro::async_latch latch(2);
    bool after1 = false;
    bool after2 = false;
    bool after3 = false;
    coro::sync_wait(coro::when_all_ready(
        [&]() -> coro::task<> {
            co_await latch;
            after1 = true;
        }(),
        [&]() -> coro::task<> {
            co_await latch;
            after2 = true;
        }(),
        [&]() -> coro::task<> {
            co_await latch;
            after3 = true;
        }(),
        [&]() -> coro::task<> {
            CHECK(!after1);
            CHECK(!after2);
            CHECK(!after3);
            latch.count_down();
            CHECK(!after1);
            CHECK(!after2);
            CHECK(!after3);
            latch.count_down();
            CHECK(after1);
            CHECK(after2);
            CHECK(after3);
            co_return;
        }()));
}

TEST_SUITE_END();
