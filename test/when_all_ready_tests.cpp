//
// Created by Jesson on 2024/10/18.
//

#include <coro/types/task.h>
#include <coro/types/shared_task.h>
#include <coro/when_all.h>
#include <coro/detail/sync_wait.h>
#include <coro/awaitable/async_manual_reset_event.h>

#include <vector>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("when_all_ready");

template<template<typename T> class TASK, typename T>
TASK<T> when_event_set_return(coro::async_manual_reset_event& event, T value) {
	co_await event;
	co_return std::move(value);
}

TEST_CASE("when_all_ready() with no args") {
	[[maybe_unused]] std::tuple<> result = coro::sync_wait(coro::when_all_ready());
}

TEST_CASE("when_all_ready() with one task") {
	bool started = false;
	auto f = [&](coro::async_manual_reset_event& event) -> coro::task<> {
		started = true;
		co_await event;
	};

	coro::async_manual_reset_event event;
	auto when_all_awaitable = coro::when_all_ready(f(event));
	CHECK(!started);

	bool finished = false;
	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto&[t] = co_await when_all_awaitable;
		finished = true;
		t.result();
	}(),
		[&]() -> coro::task<> {
		CHECK(started);
		CHECK(!finished);
		event.set();
		CHECK(finished);
		co_return;
	}()));
}

TEST_CASE("when_all_ready() with multiple task") {
	auto make_task = [&](bool& started, coro::async_manual_reset_event& event, int result) -> coro::task<int> {
		started = true;
		co_await event;
		co_return result;
	};

	coro::async_manual_reset_event event1;
	coro::async_manual_reset_event event2;
	bool started1 = false;
	bool started2 = false;
	auto when_all_awaitable = coro::when_all_ready(
		make_task(started1, event1, 1),
		make_task(started2, event2, 2));
	CHECK(!started1);
	CHECK(!started2);

	bool when_all_awaitable_finished = false;

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto[t1, t2] = co_await std::move(when_all_awaitable);
		when_all_awaitable_finished = true;
		CHECK(t1.result() == 1);
		CHECK(t2.result() == 2);
	}(),
		[&]() -> coro::task<> {
		CHECK(started1);
		CHECK(started2);

		event2.set();

		CHECK(!when_all_awaitable_finished);

		event1.set();

		CHECK(when_all_awaitable_finished);

		co_return;
	}()));
}

TEST_CASE("when_all_ready() with all task types") {
	coro::async_manual_reset_event event;
	auto t0 = when_event_set_return<coro::task>(event, 1);
	auto t1 = when_event_set_return<coro::shared_task>(event, 2);

	auto all_task = coro::when_all_ready(std::move(t0), t1);

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto [r0, r1] = co_await std::move(all_task);

		CHECK(r0.result() == 1);
		CHECK(r1.result() == 2);
	}(),
		[&]() -> coro::task<> {
		event.set();
		co_return;
	}()));
}

TEST_CASE("when_all_ready() with std::vector<task<T>>") {
	coro::async_manual_reset_event event;

	std::uint32_t started_count = 0;
	std::uint32_t finished_count = 0;

	auto make_task = [&]() -> coro::task<> {
		++started_count;
		co_await event;
		++finished_count;
	};

	std::vector<coro::task<>> tasks;
	for (std::uint32_t i = 0; i < 10; ++i) {
		tasks.emplace_back(make_task());
	}

	auto all_task = coro::when_all_ready(std::move(tasks));

	// Shouldn't have started any tasks yet.
	CHECK(started_count == 0u);

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto resultTasks = co_await std::move(all_task);
		CHECK(resultTasks.size() == 10u);

		for (auto& t : resultTasks) {
			CHECK_NOTHROW(t.result());
		}
	}(),
		[&]() -> coro::task<> {
		CHECK(started_count == 10u);
		CHECK(finished_count == 0u);

		event.set();

		CHECK(finished_count == 10u);

		co_return;
	}()));
}

TEST_CASE("when_all_ready() with std::vector<shared_task<T>>") {
	coro::async_manual_reset_event event;

	std::uint32_t started_count = 0;
	std::uint32_t finished_count = 0;

	auto make_task = [&]() -> coro::shared_task<> {
		++started_count;
		co_await event;
		++finished_count;
	};

	std::vector<coro::shared_task<>> tasks;
	for (std::uint32_t i = 0; i < 10; ++i) {
		tasks.emplace_back(make_task());
	}

	auto all_task = coro::when_all_ready(std::move(tasks));

	// Shouldn't have started any tasks yet.
	CHECK(started_count == 0u);

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto resultTasks = co_await std::move(all_task);
		CHECK(resultTasks.size() == 10u);

		for (auto& t : resultTasks) {
			CHECK_NOTHROW(t.result());
		}
	}(),
		[&]() -> coro::task<> {
		CHECK(started_count == 10u);
		CHECK(finished_count == 0u);

		event.set();

		CHECK(finished_count == 10u);

		co_return;
	}()));
}

TEST_CASE("when_all_ready() doesn't rethrow exceptions") {
	auto make_task = [](bool throw_exception) -> coro::task<int> {
		if (throw_exception) {
			throw std::exception{};
		}
		else {
			co_return 123;
		}
	};

	coro::sync_wait([&]() -> coro::task<> {
		try {
			auto[t0, t1] = co_await coro::when_all_ready(make_task(true), make_task(false));

			// You can obtain the exceptions by re-awaiting the returned tasks.
			CHECK_THROWS_AS(t0.result(), const std::exception&);
			CHECK(t1.result() == 123);
		}
		catch (...) {
			FAIL("Shouldn't throw");
		}
	}());
}

TEST_SUITE_END();
