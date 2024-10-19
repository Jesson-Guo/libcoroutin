//
// Created by Jesson on 2024/10/18.
//

#include <coro/types/shared_task.h>
#include <coro/types/task.h>
#include <coro/awaitable/single_consumer_event.h>
#include <coro/detail/sync_wait.h>
#include <coro/fmap.h>
#include <coro/when_all_ready.h>

#include "counted.h"

#include <ostream>
#include <string>
#include <type_traits>

#include "doctest/doctest.h"

TEST_SUITE_BEGIN("shared_task");

TEST_CASE("awaiting default-constructed task throws broken_promise") {
	sync_wait([]() -> coro::task<> {
		CHECK_THROWS_AS(co_await coro::shared_task<>{}, const coro::broken_promise&);
	}());
}

TEST_CASE("coroutine doesn't start executing until awaited") {
	bool started_executing = false;
	auto f = [&]() -> coro::shared_task<> {
		started_executing = true;
		co_return;
	};

	auto t = f();

	CHECK(!t.is_ready());
	CHECK(!started_executing);

    auto awaitable = [](coro::shared_task<> st) -> coro::task<> {
        co_await st;
    };
	coro::sync_wait(awaitable(t));

	CHECK(t.is_ready());
	CHECK(started_executing);
}

TEST_CASE("result is destroyed when last reference is destroyed") {
	counted::reset_counts();

	{
		auto t = []() -> coro::shared_task<counted> {
			co_return counted{};
		}();

		CHECK(counted::active_count() == 0);

		coro::sync_wait(t);

		CHECK(counted::active_count() == 1);
	}

	CHECK(counted::active_count() == 0);
}

TEST_CASE("multiple awaiters") {
	coro::single_consumer_event event;
	bool started_execution = false;
	auto produce = [&]() -> coro::shared_task<int> {
		started_execution = true;
		co_await event;
		co_return 1;
	};

	auto consume = [](coro::shared_task<int> t) -> coro::task<> {
		int result = co_await t;
		CHECK(result == 1);
	};

	auto one_task = produce();

	coro::sync_wait(when_all_ready(
		consume(one_task),
		consume(one_task),
		consume(one_task),
		[&]() -> coro::task<> {
			event.set();
			CHECK(one_task.is_ready());
			co_return;
		}()));

	CHECK(one_task.is_ready());
}

TEST_CASE("waiting on shared_task in loop doesn't cause stack-overflow") {
	// This test checks that awaiting a shared_task that completes
	// synchronously doesn't recursively resume the awaiter inside the
	// call to start executing the task. If it were to do this then we'd
	// expect that this test would result in failure due to stack-overflow.

	auto completes_synchronously = []() -> coro::shared_task<int> {
		co_return 1;
	};

	sync_wait([&]() -> coro::task<> {
		int result = 0;
		for (int i = 0; i < 1'000'000; ++i) {
			result += co_await completes_synchronously();
		}
		CHECK(result == 1'000'000);
	}());
}

TEST_CASE("make_shared_task") {
	bool started_execution = false;

	auto f = [&]() -> coro::task<std::string> {
		started_execution = false;
		co_return "test";
	};

	auto t = f();

	coro::shared_task<std::string> shared_t = coro::make_shared_task(std::move(t));

	CHECK(!shared_t.is_ready());
	CHECK(!started_execution);

	auto consume = [](coro::shared_task<std::string> t) -> coro::task<> {
		auto x = co_await std::move(t);
		CHECK(x == "test");
	};

	coro::sync_wait(when_all_ready(consume(shared_t), consume(shared_t)));
}

TEST_CASE("make_shared_task of void"
	* doctest::description{ "Tests that workaround for 'co_return <void-expr>' bug is operational if required" }) {
	bool started_execution = false;

	auto f = [&]() -> coro::task<> {
		started_execution = true;
		co_return;
	};

	auto t = f();

	coro::shared_task<> shared_t = coro::make_shared_task(std::move(t));

	CHECK(!shared_t.is_ready());
	CHECK(!started_execution);

	auto consume = [](coro::shared_task<> t) -> coro::task<> { co_await t; };

	auto c1 = consume(shared_t);
	sync_wait(c1);

	CHECK(started_execution);

	auto c2 = consume(shared_t);
	sync_wait(c2);

	CHECK(c1.is_ready());
	CHECK(c2.is_ready());
}

TEST_CASE("shared_task<void> fmap operator") {
	coro::single_consumer_event event;
	int value = 0;

	auto set_number = [&]() -> coro::shared_task<> {
		co_await event;
		value = 123;
	};

	coro::sync_wait(when_all_ready(
	    [&]() -> coro::task<> {
		    auto numeric_string_task =
			    set_number() | coro::fmap([&]() { return std::to_string(value); });

		    CHECK(co_await numeric_string_task == "123");
	    }(),
		[&]() -> coro::task<> {
		    CHECK(value == 0);
		    event.set();
		    CHECK(value == 123);
		    co_return;
	    }()
	));
}

TEST_CASE("shared_task<T> fmap operator") {
	coro::single_consumer_event event;

	auto getNumber = [&]() -> coro::shared_task<int> {
		co_await event;
		co_return 123;
	};

	coro::sync_wait(when_all_ready(
		[&]() -> coro::task<> {
		    auto numeric_string_task =
			    getNumber() | coro::fmap([](int x) { return std::to_string(x); });

		    CHECK(co_await numeric_string_task == "123");
	    }(),
		[&]() -> coro::task<> {
		    event.set();
		    co_return;
	    }()
	));
}

TEST_SUITE_END();
