//
// Created by Jesson on 2024/10/18.
//

#include <coro/detail/sync_wait.h>
#include <coro/types/task.h>
#include <coro/types/shared_task.h>
#include <coro/thread_pool.h>

#include <string>
#include <type_traits>

#include "doctest/doctest.h"

TEST_SUITE_BEGIN("sync_wait");

static_assert(std::is_same_v<
    decltype(sync_wait(std::declval<coro::task<std::string>>())),
    std::string&&>);
static_assert(std::is_same_v<
    decltype(sync_wait(std::declval<coro::task<std::string>&>())),
    std::string&>);

TEST_CASE("sync_wait(task<T>)") {
    auto make_task = []() -> coro::task<std::string> {
        co_return "foo";
    };

    auto task = make_task();
    CHECK(coro::sync_wait(task) == "foo");
    CHECK(coro::sync_wait(make_task()) == "foo");
}

TEST_CASE("sync_wait(shared_task<T>)") {
    auto make_task = []() -> coro::shared_task<std::string> {
        co_return "foo";
    };

    auto task = make_task();

    CHECK(coro::sync_wait(task) == "foo");
    CHECK(coro::sync_wait(make_task()) == "foo");
}

TEST_CASE("multiple threads") {
    coro::thread_pool tp{ 3 };

    int value = 0;
    auto create_lazy_task = [&]() -> coro::task<int> {
        co_await tp.schedule();
        co_return value++;
    };

    for (int i = 0; i < 10; ++i) {
        CHECK(coro::sync_wait(create_lazy_task()) == i);
    }
}

TEST_SUITE_END();
