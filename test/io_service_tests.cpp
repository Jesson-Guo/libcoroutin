//
// Created by Jesson on 2024/10/18.
//

#include <coro/io/io_service.h>
#include <coro/types/task.h>
#include <coro/detail/sync_wait.h>
#include <coro/when_all.h>
#include <coro/when_all_ready.h>
#include <coro/on_scope_exit.h>
#include <coro/operation_cancelled.h>
#include <coro/cancellation/cancellation_source.h>

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

    ~io_service_fixture() {
        stop();
    }

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

TEST_SUITE_BEGIN("io_service");

TEST_CASE("construct") {
	coro::io_service service;
	CHECK_FALSE(service.is_stop_requested());
}

TEST_CASE("process_one_pending_event returns immediately when no events") {
	coro::io_service service;
	CHECK(service.process_one_pending_event() == 0);
	CHECK(service.process_pending_events() == 0);
}

TEST_CASE("schedule coroutine") {
	coro::io_service service;
	bool reached_point_a = false;
	bool reached_point_b = false;
	auto start_task = [&](coro::io_service& io_svc) -> coro::task<> {
		reached_point_a = true;
		co_await io_svc.schedule();
		reached_point_b = true;
	};

	coro::sync_wait(when_all_ready(
		start_task(service),
		[&]() -> coro::task<> {
			CHECK(reached_point_a);
			CHECK_FALSE(reached_point_b);
		    std::this_thread::sleep_for(std::chrono::milliseconds(500));
			service.process_pending_events();
			CHECK(reached_point_b);
			co_return;
		}()));
}

using multi_io_thread_servicing_events_fixture = io_service_fixture_with_threads<2>;

TEST_CASE_FIXTURE(multi_io_thread_servicing_events_fixture, "multiple I/O threads servicing events") {
	std::atomic completed_count = 0;

    constexpr int operations = 1000;

	auto run_on_io_thread = [&]() -> coro::task<> {
		co_await io_service().schedule();
		++completed_count;
	};

	std::vector<coro::task<>> tasks;
    tasks.reserve(operations);
    for (int i = 0; i < operations; ++i) {
        tasks.emplace_back(run_on_io_thread());
    }

	sync_wait(when_all(std::move(tasks)));

	CHECK(completed_count == operations);
}

TEST_SUITE_END();
