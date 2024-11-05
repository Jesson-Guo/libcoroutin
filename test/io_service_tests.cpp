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

TEST_CASE("Multiple concurrent timers") {
	coro::io_service service;

	auto start_timer = [&](std::chrono::milliseconds duration)
		-> coro::task<std::chrono::high_resolution_clock::duration> {
		auto start = std::chrono::high_resolution_clock::now();
		co_await service.schedule_after(duration);
		auto end = std::chrono::high_resolution_clock::now();
		co_return end - start;
	};

	auto test = [&]() -> coro::task<> {
		using namespace std::chrono;
		using namespace std::chrono_literals;

		auto[time1, time2, time3] = co_await coro::when_all(
			start_timer(100ms),
			start_timer(120ms),
			start_timer(50ms));

		MESSAGE("Waiting 100ms took " << duration_cast<microseconds>(time1).count() << "us");
		MESSAGE("Waiting 120ms took " << duration_cast<microseconds>(time2).count() << "us");
		MESSAGE("Waiting 50ms took " << duration_cast<microseconds>(time3).count() << "us");

		CHECK(time1 >= 100ms);
		CHECK(time2 >= 120ms);
		CHECK(time3 >= 50ms);
	};

	coro::sync_wait(coro::when_all_ready(
		[&]() -> coro::task<> {
			auto stopIoOnExit = coro::on_scope_exit([&] { service.stop(); });
			co_await test();
		}(),
		[&]() -> coro::task<> {
			service.process_events();
			co_return;
		}()));
}

TEST_CASE("Timer cancellation" * doctest::timeout{ 5.0 }) {
	using namespace std::literals::chrono_literals;
	coro::io_service service;

	auto long_wait = [&](coro::cancellation_token ct) -> coro::task<> {
		co_await service.schedule_after(20'000ms, ct);
	};

	auto cancel_after = [&](coro::cancellation_source source, auto duration) -> coro::task<> {
		co_await service.schedule_after(duration);
		source.request_cancellation();
	};

	auto test = [&]() -> coro::task<> {
		coro::cancellation_source source;
		co_await coro::when_all_ready(
			[&](coro::cancellation_token ct) -> coro::task<> {
			    CHECK_THROWS_AS(co_await long_wait(std::move(ct)), const coro::operation_cancelled&);
		    }(source.token()),
			cancel_after(source, 1ms)
		);
	};

	auto test_twice = [&]() -> coro::task<> {
		co_await test();
		co_await test();
	};

	auto stop_io_service_after = [&](coro::task<> task) -> coro::task<> {
		co_await task.when_ready();
		service.stop();
		co_return co_await task.when_ready();
	};

	coro::sync_wait(coro::when_all_ready(
		stop_io_service_after(test_twice()),
		[&]() -> coro::task<> {
			service.process_events();
			co_return;
		}()));
}

using many_concurrent_fixture = io_service_fixture_with_threads<1>;

TEST_CASE_FIXTURE(many_concurrent_fixture, "Many concurrent timers") {
	auto start_timer = [&]() -> coro::task<> {
		using namespace std::literals::chrono_literals;
		co_await io_service().schedule_after(50ms);
	};

	constexpr std::uint32_t task_count = 10'000;

	auto run_many_timers = [&]() -> coro::task<> {
		std::vector<coro::task<>> tasks;
		tasks.reserve(task_count);
		for (std::uint32_t i = 0; i < task_count; ++i) {
			tasks.emplace_back(start_timer());
		}

		co_await coro::when_all(std::move(tasks));
	};

	auto start = std::chrono::high_resolution_clock::now();
	coro::sync_wait(run_many_timers());
	auto end = std::chrono::high_resolution_clock::now();
	MESSAGE(
		"Waiting for " << task_count << " x 50ms timers took "
		<< std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
		<< "ms");
}

TEST_SUITE_END();
