//
// Created by Jesson on 2024/10/18.
//

#include <coro/types/task.h>
#include <coro/types/shared_task.h>
#include <coro/when_all.h>
#include <coro/detail/sync_wait.h>
#include <coro/awaitable/async_manual_reset_event.h>
#include <coro/awaitable/async_mutex.h>
#include <coro/fmap.h>

#include "counted.h"

#include <functional>
#include <string>
#include <vector>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("when_all");

namespace {
	template<template<typename T> class TASK, typename T>
	TASK<T> when_event_set_return(coro::async_manual_reset_event& event, T value) {
		co_await event;
		co_return std::move(value);
	}
}

TEST_CASE("when_all() with no args completes immediately") {
	[[maybe_unused]] std::tuple<> result = coro::sync_wait(coro::when_all());
}

TEST_CASE("when_all() with one arg") {
	bool started = false;
	bool finished = false;
	auto f = [&](coro::async_manual_reset_event& event) -> coro::task<std::string> {
		started = true;
		co_await event;
		finished = true;
		co_return "foo";
	};

	coro::async_manual_reset_event event;

	auto when_all_task = coro::when_all(f(event));
	CHECK(!started);

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto[s] = co_await when_all_task;
		CHECK(s == "foo");
	}(),
		[&]() -> coro::task<> {
		CHECK(started);
		CHECK(!finished);
		event.set();
		CHECK(finished);
		co_return;
	}()));
}

TEST_CASE("when_all() with awaitables") {
	coro::sync_wait([]() -> coro::task<> {
		auto make_task = [](int x) -> coro::task<int> { co_return x; };

		coro::async_manual_reset_event event;
		event.set();

		coro::async_mutex mutex;

		auto[event_result, mutex_lock, number] = co_await coro::when_all(
			std::ref(event),
			mutex.scoped_lock_async(),
			make_task(123) | coro::fmap([](int x) { return x + 1; }));

		(void)event_result;
		(void)mutex_lock;
		CHECK(number == 124);
		CHECK(!mutex.try_lock());
	}());
}

TEST_CASE("when_all() with all task types") {
	counted::reset_counts();

	auto run = [](coro::async_manual_reset_event& event) -> coro::task<> {
		using namespace std::string_literals;

		auto[a, b] = co_await coro::when_all(
			when_event_set_return<coro::task>(event, "foo"s),
			when_event_set_return<coro::shared_task>(event, counted{}));

		CHECK(a == "foo");
		CHECK(b.id == 0);
        CHECK(counted::active_count() == 1);
	};

	coro::async_manual_reset_event event;

	coro::sync_wait(coro::when_all_ready(
		run(event),
		[&]() -> coro::task<> {
		event.set();
		co_return;
	}()));
}

TEST_CASE("when_all() throws if any task throws") {
	struct X {};
	struct Y {};

	int started_count = 0;
	auto make_task = [&](int value) -> coro::task<int> {
		++started_count;
		if (value == 0) throw X{};
		else if (value == 1) throw Y{};
		else co_return value;
	};

	coro::sync_wait([&]() -> coro::task<> {
		try {
			(void)co_await coro::when_all(make_task(0), make_task(1), make_task(2));
		}
		catch (const X&) {}
		catch (const Y&) {}
	}());
}

TEST_CASE("when_all() with task<void>") {
	int void_task_count = 0;
	auto make_void_task = [&]() -> coro::task<> {
		++void_task_count;
		co_return;
	};

	auto make_int_task = [](int x) -> coro::task<int> { co_return x; };

	// Single void task in when_all()
	auto[x] = coro::sync_wait(coro::when_all(make_void_task()));
	(void)x;
	CHECK(void_task_count == 1);

	// Multiple void tasks in when_all()
	auto[a, b] = coro::sync_wait(coro::when_all(
		make_void_task(),
		make_void_task()));
	(void)a;
	(void)b;
	CHECK(void_task_count == 3);

	// Mixing void and non-void tasks in when_all()
	auto[v1, i, v2] = coro::sync_wait(coro::when_all(
		make_void_task(),
		make_int_task(123),
		make_void_task()));
	(void)v1;
	(void)v2;
	CHECK(void_task_count == 5);

	CHECK(i == 123);
}

TEST_CASE("when_all() with vector<task<>>") {
	int started_count = 0;
	auto make_task = [&](coro::async_manual_reset_event& event) -> coro::task<> {
		++started_count;
		co_await event;
	};

	coro::async_manual_reset_event event1;
	coro::async_manual_reset_event event2;

	bool finished = false;

	auto run = [&]() -> coro::task<> {
		std::vector<coro::task<>> tasks;
		tasks.push_back(make_task(event1));
		tasks.push_back(make_task(event2));
		tasks.push_back(make_task(event1));

		auto all_task = coro::when_all(std::move(tasks));

		CHECK(started_count == 0);

		co_await all_task;

		finished = true;
	};

	coro::sync_wait(coro::when_all_ready(
		run(),
		[&]() -> coro::task<> {
		CHECK(started_count == 3);
		CHECK(!finished);

		event1.set();

		CHECK(!finished);

		event2.set();

		CHECK(finished);
		co_return;
	}()));
}

TEST_CASE("when_all() with vector<shared_task<>>") {
	int started_count = 0;
	auto make_task = [&](coro::async_manual_reset_event& event) -> coro::shared_task<> {
		++started_count;
		co_await event;
	};

	coro::async_manual_reset_event event1;
	coro::async_manual_reset_event event2;

	bool finished = false;

	auto run = [&]() -> coro::task<> {
		std::vector<coro::shared_task<>> tasks;
		tasks.push_back(make_task(event1));
		tasks.push_back(make_task(event2));
		tasks.push_back(make_task(event1));

		auto all_task = coro::when_all(std::move(tasks));

		CHECK(started_count == 0);

		co_await all_task;

		finished = true;
	};

	coro::sync_wait(coro::when_all_ready(
		run(),
		[&]() -> coro::task<> {
		CHECK(started_count == 3);
		CHECK(!finished);

		event1.set();

		CHECK(!finished);

		event2.set();

		CHECK(finished);

		co_return;
	}()));
}

namespace {
	template<template<typename T> class TASK>
	void check_when_all_vector_of_task_value() {
		coro::async_manual_reset_event event1;
		coro::async_manual_reset_event event2;

		bool when_all_completed = false;

		coro::sync_wait(coro::when_all_ready(
			[&]() -> coro::task<> {
			std::vector<TASK<int>> tasks;

			tasks.emplace_back(when_event_set_return<TASK>(event1, 1));
			tasks.emplace_back(when_event_set_return<TASK>(event2, 2));

			auto when_all_task = coro::when_all(std::move(tasks));

			auto values = co_await when_all_task;
			REQUIRE(values.size() == 2);
			CHECK(values[0] == 1);
			CHECK(values[1] == 2);

			when_all_completed = true;
		}(),
			[&]() -> coro::task<> {
			CHECK(!when_all_completed);
			event2.set();
			CHECK(!when_all_completed);
			event1.set();
			CHECK(when_all_completed);
			co_return;
		}()));
	}
}

constexpr bool is_optimised = false;

TEST_CASE("when_all() with vector<task<T>>") {
	check_when_all_vector_of_task_value<coro::task>();
}

TEST_CASE("when_all() with vector<shared_task<T>>") {
	check_when_all_vector_of_task_value<coro::shared_task>();
}

namespace {
	template<template<typename T> class TASK>
	void check_when_all_vector_of_task_reference() {
		coro::async_manual_reset_event event1;
		coro::async_manual_reset_event event2;

		int value1 = 1;
		int value2 = 2;

		auto make_task = [](coro::async_manual_reset_event& event, int& value) -> TASK<int&> {
			co_await event;
			co_return value;
		};

		bool when_all_complete = false;

		coro::sync_wait(coro::when_all_ready(
		    [&]() -> coro::task<> {
			std::vector<TASK<int&>> tasks;
			tasks.emplace_back(make_task(event1, value1));
			tasks.emplace_back(make_task(event2, value2));

			auto when_all_task = coro::when_all(std::move(tasks));

			std::vector<std::reference_wrapper<int>> values = co_await when_all_task;
			REQUIRE(values.size() == 2);
			CHECK(&values[0].get() == &value1);
			CHECK(&values[1].get() == &value2);

			when_all_complete = true;
		}(),
			[&]() -> coro::task<> {
			CHECK(!when_all_complete);
			event2.set();
			CHECK(!when_all_complete);
			event1.set();
			CHECK(when_all_complete);
			co_return;
		}()));
	}
}

TEST_CASE("when_all() with vector<task<T&>>") {
	check_when_all_vector_of_task_reference<coro::task>();
}

TEST_CASE("when_all() with vector<shared_task<T&>>") {
	check_when_all_vector_of_task_reference<coro::shared_task>();
}

TEST_SUITE_END();