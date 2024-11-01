//
// Created by Jesson on 2024/10/24.
//

#include <coro/schedule_on.h>
#include <coro/resume_on.h>
#include <coro/io/io_service.h>
#include <coro/detail/sync_wait.h>
#include <coro/when_all_ready.h>
#include <coro/on_scope_exit.h>
#include <coro/fmap.h>

#include <thread>
#include <vector>

#include <ostream>
#include "doctest/doctest.h"

/// \brief
/// Test fixture that creates an io_service and starts up a background thread
/// to process I/O completion events.
///
/// Thread and io_service are shutdown on destruction.
struct io_service_fixture {
    explicit io_service_fixture(std::uint32_t thread_count = 1) {
        m_io_threads.reserve(thread_count);
        try {
            for (std::uint32_t i = 0; i < thread_count; ++i) {
                m_io_threads.emplace_back([this] { m_io_service.process_events(); });
            }
        }
        catch (...) {
            stop();
            throw;
        }
    }

    ~io_service_fixture() { stop(); }

    coro::io_service& io_service() { return m_io_service; }

private:
    void stop() {
        m_io_service.stop();
        for (auto& thread : m_io_threads) {
            thread.join();
        }
    }

    coro::io_service m_io_service;
    std::vector<std::thread> m_io_threads;
};

template<std::uint32_t thread_count>
struct io_service_fixture_with_threads : io_service_fixture {
    io_service_fixture_with_threads() : io_service_fixture(thread_count) {}
};

TEST_SUITE_BEGIN("schedule/resume_on");

TEST_CASE_FIXTURE(io_service_fixture, "schedule_on task<> function") {
	auto main_thread_id = std::this_thread::get_id();

	std::thread::id io_thread_id;

	auto start = [&]() -> coro::task<> {
		io_thread_id = std::this_thread::get_id();
		CHECK(io_thread_id != main_thread_id);
		co_return;
	};

	sync_wait([&]() -> coro::task<> {
		CHECK(std::this_thread::get_id() == main_thread_id);
		co_await schedule_on(io_service(), start());
	}());
}

TEST_CASE_FIXTURE(io_service_fixture, "schedule_on async_generator<> function") {
	auto main_thread_id = std::this_thread::get_id();

	std::thread::id io_thread_id;

	auto make_sequence = [&]() -> coro::async_generator<int> {
		io_thread_id = std::this_thread::get_id();
		CHECK(io_thread_id != main_thread_id);
		co_yield 1;
		CHECK(std::this_thread::get_id() == io_thread_id);
		co_yield 2;
		CHECK(std::this_thread::get_id() == io_thread_id);
		co_yield 3;
		CHECK(std::this_thread::get_id() == io_thread_id);
		co_return;
	};

	coro::io_service other_io_service;

	coro::sync_wait(when_all_ready(
		[&]() -> coro::task<> {
		CHECK(std::this_thread::get_id() == main_thread_id);

		auto seq = schedule_on(io_service(), make_sequence());

		int expected = 1;
		for (auto iter = co_await seq.begin(); iter != seq.end(); co_await ++iter) {
			int value = *iter;
			CHECK(value == expected++);

			// Transfer exection back to main thread before
			// awaiting next item in the loop to chck that
			// the generator is resumed on io_service() thread.
			co_await other_io_service.schedule();
		}

		other_io_service.stop();
	}(),
		[&]() -> coro::task<> {
		other_io_service.process_events();
		co_return;
	}()));
}

TEST_CASE_FIXTURE(io_service_fixture, "resume_on task<> function") {
	auto main_thread_id = std::this_thread::get_id();

	auto start = [&]() -> coro::task<> {
		CHECK(std::this_thread::get_id() == main_thread_id);
		co_return;
	};

	coro::sync_wait([&]() -> coro::task<> {
		CHECK(std::this_thread::get_id() == main_thread_id);

		co_await resume_on(io_service(), start());

		// NOTE: This check could potentially spuriously fail with the current
		// implementation of task<T>. See coro issue #79.
		CHECK(std::this_thread::get_id() != main_thread_id);
	}());
}

TEST_CASE_FIXTURE(io_service_fixture, "resume_on async_generator<> function") {
	auto main_thread_id = std::this_thread::get_id();

	std::thread::id io_thread_id;

	auto make_sequence = [&]() -> coro::async_generator<int> {
		co_await io_service().schedule();
		io_thread_id = std::this_thread::get_id();
		CHECK(io_thread_id != main_thread_id);
		co_yield 1;
		co_yield 2;
		co_await io_service().schedule();
		co_yield 3;
		co_await io_service().schedule();
		co_return;
	};

	coro::io_service other_io_service;

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto stopOnExit = coro::on_scope_exit([&] { other_io_service.stop(); });

		CHECK(std::this_thread::get_id() == main_thread_id);

		auto seq = resume_on(other_io_service, make_sequence());

		int expected = 1;
		for (auto iter = co_await seq.begin(); iter != seq.end(); co_await ++iter) {
			int value = *iter;
			// Every time we receive a value it should be on our requested
			// scheduler (ie. main thread)
			CHECK(std::this_thread::get_id() == main_thread_id);
			CHECK(value == expected++);

			// Occasionally transfer execution to a different thread before
			// awaiting next element.
			if (value == 2) {
				co_await io_service().schedule();
			}
		}

		other_io_service.stop();
	}(),
		[&]() -> coro::task<> {
		other_io_service.process_events();
		co_return;
	}()));
}

TEST_CASE_FIXTURE(io_service_fixture, "schedule_on task<> pipe syntax") {
	auto main_thread_id = std::this_thread::get_id();

	auto make_task = [&]() -> coro::task<int> {
		CHECK(std::this_thread::get_id() != main_thread_id);
		co_return 123;
	};

	auto triple = [&](int x) {
		CHECK(std::this_thread::get_id() != main_thread_id);
		return x * 3;
	};

	CHECK(coro::sync_wait(make_task() | schedule_on(io_service())) == 123);

	// Shouldn't matter where in sequence schedule_on() appears since it applies
	// at the start of the pipeline (ie. before first task starts).
	CHECK(coro::sync_wait(make_task() | schedule_on(io_service()) | coro::fmap(triple)) == 369);
	CHECK(coro::sync_wait(make_task() | coro::fmap(triple) | schedule_on(io_service())) == 369);
}

TEST_CASE_FIXTURE(io_service_fixture, "resume_on task<> pipe syntax") {
	auto main_thread_id = std::this_thread::get_id();

	auto make_task = [&]() -> coro::task<int> {
		CHECK(std::this_thread::get_id() == main_thread_id);
		co_return 123;
	};

	coro::sync_wait([&]() -> coro::task<> {
		coro::task<int> t = make_task() | coro::resume_on(io_service());
		CHECK(co_await t == 123);
		CHECK(std::this_thread::get_id() != main_thread_id);
	}());
}

TEST_CASE_FIXTURE(io_service_fixture, "resume_on task<> pipe syntax multiple uses") {
	auto main_thread_id = std::this_thread::get_id();

	auto make_task = [&]() -> coro::task<int> {
		CHECK(std::this_thread::get_id() == main_thread_id);
		co_return 123;
	};

	auto triple = [&](int x) {
		CHECK(std::this_thread::get_id() != main_thread_id);
		return x * 3;
	};

	coro::io_service other_io_service;

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
		auto stopOnExit = coro::on_scope_exit([&] { other_io_service.stop(); });

		CHECK(std::this_thread::get_id() == main_thread_id);

		coro::task<int> t =
			make_task()
			| coro::resume_on(io_service())
			| coro::fmap(triple)
			| coro::resume_on(other_io_service);

		CHECK(co_await t == 369);

		CHECK(std::this_thread::get_id() == main_thread_id);
	}(),
		[&]() -> coro::task<> {
		other_io_service.process_events();
		co_return;
	}()));
}

TEST_SUITE_END();
