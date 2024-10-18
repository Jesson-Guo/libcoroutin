//
// Created by Jesson on 2024/10/18.
//

#include <coro/cancellation/cancellation_token.h>
#include <coro/cancellation/cancellation_source.h>
#include <coro/cancellation/cancellation_registration.h>
#include <coro/operation_cancelled.h>

#include <thread>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("cancellation tests");

TEST_CASE("default cancellation_token is not cancellable") {
	coro::cancellation_token t;
	CHECK(!t.is_cancellation_requested());
	CHECK(!t.can_be_cancelled());
}

TEST_CASE("calling request_cancellation on cancellation_source updates cancellation_token") {
	coro::cancellation_source s;
	coro::cancellation_token t = s.token();
	CHECK(t.can_be_cancelled());
	CHECK(!t.is_cancellation_requested());
	s.request_cancellation();
	CHECK(t.is_cancellation_requested());
	CHECK(t.can_be_cancelled());
}

TEST_CASE("cancellation_token can't be cancelled when last cancellation_source destructed") {
	coro::cancellation_token t;
	{
		coro::cancellation_source s;
		t = s.token();
		CHECK(t.can_be_cancelled());
	}
	CHECK(!t.can_be_cancelled());
}

TEST_CASE("cancellation_token can be cancelled when last cancellation_source destructed if cancellation already requested") {
	coro::cancellation_token t;
	{
		coro::cancellation_source s;
		t = s.token();
		CHECK(t.can_be_cancelled());
		s.request_cancellation();
	}
	CHECK(t.can_be_cancelled());
	CHECK(t.is_cancellation_requested());
}

TEST_CASE("cancellation_registration when cancellation not yet requested") {
	coro::cancellation_source s;

	bool callback_executed = false;
	{
		coro::cancellation_registration callback_registration(
		    s.token(), [&] { callback_executed = true; });
	}

	CHECK(!callback_executed);

	{
		coro::cancellation_registration callbackRegistration(
			s.token(), [&] { callback_executed = true; });

		CHECK(!callback_executed);
		s.request_cancellation();
		CHECK(callback_executed);
	}
}

TEST_CASE("throw_if_cancellation_requested") {
	coro::cancellation_source s;
	coro::cancellation_token t = s.token();

	CHECK_NOTHROW(t.throw_if_cancellation_requested());
	s.request_cancellation();
	CHECK_THROWS_AS(t.throw_if_cancellation_requested(), const coro::operation_cancelled&);
}

TEST_CASE("cancellation_registration called immediately when cancellation already requested") {
	coro::cancellation_source s;
	s.request_cancellation();

	bool executed = false;
	coro::cancellation_registration r{ s.token(), [&] { executed = true; } };
	CHECK(executed);
}

TEST_CASE("register many callbacks"
	* doctest::description{
	"this checks the code-path that allocates the next chunk of entries "
	"in the internal data-structures, which occurs on 17th callback" }) {
	coro::cancellation_source s;
	auto t = s.token();

	int callback_execution_count = 0;
	auto callback = [&] { ++callback_execution_count; };

	// Allocate enough to require a second chunk to be allocated.
	coro::cancellation_registration r1{ t, callback };
	coro::cancellation_registration r2{ t, callback };
	coro::cancellation_registration r3{ t, callback };
	coro::cancellation_registration r4{ t, callback };
	coro::cancellation_registration r5{ t, callback };
	coro::cancellation_registration r6{ t, callback };
	coro::cancellation_registration r7{ t, callback };
	coro::cancellation_registration r8{ t, callback };
	coro::cancellation_registration r9{ t, callback };
	coro::cancellation_registration r10{ t, callback };
	coro::cancellation_registration r11{ t, callback };
	coro::cancellation_registration r12{ t, callback };
	coro::cancellation_registration r13{ t, callback };
	coro::cancellation_registration r14{ t, callback };
	coro::cancellation_registration r15{ t, callback };
	coro::cancellation_registration r16{ t, callback };
	coro::cancellation_registration r17{ t, callback };
	coro::cancellation_registration r18{ t, callback };

	s.request_cancellation();
	CHECK(callback_execution_count == 18);
}

TEST_CASE("concurrent registration and cancellation") {
	// Just check this runs and terminates without crashing.
	for (int i = 0; i < 100; ++i) {
		coro::cancellation_source source;

		std::thread waiter1{ [token = source.token()] {
			std::atomic cancelled = false;
			while (!cancelled) {
				coro::cancellation_registration registration{
				    token, [&] { cancelled = true; }
				};

				coro::cancellation_registration reg0{ token, [] {} };
				coro::cancellation_registration reg1{ token, [] {} };
				coro::cancellation_registration reg2{ token, [] {} };
				coro::cancellation_registration reg3{ token, [] {} };
				coro::cancellation_registration reg4{ token, [] {} };
				coro::cancellation_registration reg5{ token, [] {} };
				coro::cancellation_registration reg6{ token, [] {} };
				coro::cancellation_registration reg7{ token, [] {} };
				coro::cancellation_registration reg8{ token, [] {} };
				coro::cancellation_registration reg9{ token, [] {} };
				coro::cancellation_registration reg10{ token, [] {} };
				coro::cancellation_registration reg11{ token, [] {} };
				coro::cancellation_registration reg12{ token, [] {} };
				coro::cancellation_registration reg13{ token, [] {} };
				coro::cancellation_registration reg14{ token, [] {} };
				coro::cancellation_registration reg15{ token, [] {} };
				coro::cancellation_registration reg17{ token, [] {} };

				std::this_thread::yield();
			}
		}};

		std::thread waiter2{ [token = source.token()] {
			std::atomic cancelled = false;
			while (!cancelled) {
				coro::cancellation_registration registration{
				    token, [&]{ cancelled = true; }
				};

				coro::cancellation_registration reg0{ token, [] {} };
				coro::cancellation_registration reg1{ token, [] {} };
				coro::cancellation_registration reg2{ token, [] {} };
				coro::cancellation_registration reg3{ token, [] {} };
				coro::cancellation_registration reg4{ token, [] {} };
				coro::cancellation_registration reg5{ token, [] {} };
				coro::cancellation_registration reg6{ token, [] {} };
				coro::cancellation_registration reg7{ token, [] {} };
				coro::cancellation_registration reg8{ token, [] {} };
				coro::cancellation_registration reg9{ token, [] {} };
				coro::cancellation_registration reg10{ token, [] {} };
				coro::cancellation_registration reg11{ token, [] {} };
				coro::cancellation_registration reg12{ token, [] {} };
				coro::cancellation_registration reg13{ token, [] {} };
				coro::cancellation_registration reg14{ token, [] {} };
				coro::cancellation_registration reg15{ token, [] {} };
				coro::cancellation_registration reg16{ token, [] {} };

				std::this_thread::yield();
			}
		} };

		std::thread waiter3{ [token = source.token()] {
			std::atomic cancelled = false;
			while (!cancelled) {
				coro::cancellation_registration registration{
				    token, [&] { cancelled = true; }
				};

				coro::cancellation_registration reg0{ token, [] {} };
				coro::cancellation_registration reg1{ token, [] {} };
				coro::cancellation_registration reg2{ token, [] {} };
				coro::cancellation_registration reg3{ token, [] {} };
				coro::cancellation_registration reg4{ token, [] {} };
				coro::cancellation_registration reg5{ token, [] {} };
				coro::cancellation_registration reg6{ token, [] {} };
				coro::cancellation_registration reg7{ token, [] {} };
				coro::cancellation_registration reg8{ token, [] {} };
				coro::cancellation_registration reg9{ token, [] {} };
				coro::cancellation_registration reg10{ token, [] {} };
				coro::cancellation_registration reg11{ token, [] {} };
				coro::cancellation_registration reg12{ token, [] {} };
				coro::cancellation_registration reg13{ token, [] {} };
				coro::cancellation_registration reg14{ token, [] {} };
				coro::cancellation_registration reg15{ token, [] {} };
				coro::cancellation_registration reg16{ token, [] {} };

				std::this_thread::yield();
			}
		} };

		std::thread canceller{[&source] { source.request_cancellation(); }};

		canceller.join();
		waiter1.join();
		waiter2.join();
		waiter3.join();
	}
}

TEST_CASE("cancellation registration single-threaded performance") {
	struct batch {
		explicit batch(coro::cancellation_token t)
			: r0(t, [] {})
			, r1(t, [] {})
			, r2(t, [] {})
			, r3(t, [] {})
			, r4(t, [] {})
			, r5(t, [] {})
			, r6(t, [] {})
			, r7(t, [] {})
			, r8(t, [] {})
			, r9(t, [] {}) {}

		coro::cancellation_registration r0;
		coro::cancellation_registration r1;
		coro::cancellation_registration r2;
		coro::cancellation_registration r3;
		coro::cancellation_registration r4;
		coro::cancellation_registration r5;
		coro::cancellation_registration r6;
		coro::cancellation_registration r7;
		coro::cancellation_registration r8;
		coro::cancellation_registration r9;
	};

	coro::cancellation_source s;
	constexpr int iteration_count = 100'000;
	auto start = std::chrono::high_resolution_clock::now();

	for (int i = 0; i < iteration_count; ++i) {
		coro::cancellation_registration r{ s.token(), [] {} };
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto time1 = end - start;
	start = end;

	for (int i = 0; i < iteration_count; ++i) {
		batch b{ s.token() };
	}

	end = std::chrono::high_resolution_clock::now();
	auto time2 = end - start;
	start = end;

	for (int i = 0; i < iteration_count; ++i) {
		batch b0{ s.token() };
		batch b1{ s.token() };
		batch b2{ s.token() };
		batch b3{ s.token() };
		batch b4{ s.token() };
	}

	end = std::chrono::high_resolution_clock::now();
	auto time3 = end - start;

	auto report = [](const char* label, auto time, std::uint64_t count) {
		auto us = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
		MESSAGE(label << " took " << us << "us (" << (1000.0 * us / count) << " ns/item)");
	};

	report("Individual", time1, iteration_count);
	report("Batch10", time2, 10 * iteration_count);
	report("Batch50", time3, 50 * iteration_count);
}
