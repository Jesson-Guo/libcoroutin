//
// Created by Jesson on 2024/10/18.
//

#include <coro/types/async_generator.h>
#include <coro/awaitable/single_consumer_event.h>
#include <coro/types/task.h>
#include <coro/detail/sync_wait.h>
#include <coro/when_all.h>

#include <ostream>
#include "doctest/doctest.h"

TEST_SUITE_BEGIN("async_generator");

TEST_CASE("default-constructed async_generator is an empty sequence") {
	coro::sync_wait([]() -> coro::task<> {
		// Iterating over default-constructed async_generator just
		// gives an empty sequence.
		coro::async_generator<int> g;
		auto it = co_await g.begin();
		CHECK(it == g.end());
	}());
}

TEST_CASE("async_generator doesn't start if begin() not called") {
	bool started_execution = false;
	{
		auto gen = [&]() -> coro::async_generator<int> {
			started_execution = true;
			co_yield 1;
		}();
		CHECK(!started_execution);
	}
	CHECK(!started_execution);
}

TEST_CASE("enumerate sequence of 1 value") {
	coro::sync_wait([]() -> coro::task<> {
		bool started_execution = false;
		auto make_generator = [&]() -> coro::async_generator<std::uint32_t> {
			started_execution = true;
			co_yield 1;
		};

		auto gen = make_generator();

		CHECK(!started_execution);

		auto it = co_await gen.begin();

		CHECK(started_execution);
		CHECK(it != gen.end());
		CHECK(*it == 1u);
		CHECK(co_await ++it == gen.end());
	}());
}

TEST_CASE("enumerate sequence of multiple values") {
	coro::sync_wait([]() -> coro::task<> {
		bool started_execution = false;
		auto make_generator = [&]() -> coro::async_generator<std::uint32_t> {
			started_execution = true;
			co_yield 1;
			co_yield 2;
			co_yield 3;
		};

		auto gen = make_generator();

		CHECK(!started_execution);

		auto it = co_await gen.begin();

		CHECK(started_execution);

		CHECK(it != gen.end());
		CHECK(*it == 1u);

		CHECK(co_await ++it != gen.end());
		CHECK(*it == 2u);

		CHECK(co_await ++it != gen.end());
		CHECK(*it == 3u);

		CHECK(co_await ++it == gen.end());
	}());
}

namespace {

class set_to_true_on_destruction {
public:
    explicit set_to_true_on_destruction(bool* value) : m_value(value) {}
    set_to_true_on_destruction(set_to_true_on_destruction&& other) noexcept : m_value(other.m_value) {
        other.m_value = nullptr;
    }
    ~set_to_true_on_destruction() {
        if (m_value != nullptr) {
            *m_value = true;
        }
    }
    set_to_true_on_destruction(const set_to_true_on_destruction&) = delete;
    set_to_true_on_destruction& operator=(const set_to_true_on_destruction&) = delete;
private:
    bool* m_value;
};

}

TEST_CASE("destructors of values in scope are called when async_generator destructed early") {
	coro::sync_wait([]() -> coro::task<> {
		bool a_destructed = false;
	    bool b_destructed = false;

        {
            auto make_generator = [&](set_to_true_on_destruction a) -> coro::async_generator<std::uint32_t> {
                set_to_true_on_destruction b(&b_destructed);
                co_yield 1;
                co_yield 2;
            };
            auto gen = make_generator(set_to_true_on_destruction(&a_destructed));

			CHECK(!a_destructed);
			CHECK(!b_destructed);

			auto it = co_await gen.begin();
			CHECK(!a_destructed);
			CHECK(!b_destructed);
			CHECK(*it == 1u);
		}

		CHECK(a_destructed);
		CHECK(b_destructed);
	}());
}

TEST_CASE("async producer with async consumer"
	* doctest::description{
		"This test tries to cover the different state-transition code-paths\n"
		"- consumer resuming producer and producer completing asynchronously\n"
		"- producer resuming consumer and consumer requesting next value synchronously\n"
		"- producer resuming consumer and consumer requesting next value asynchronously" }) {
	coro::single_consumer_event p1;
	coro::single_consumer_event p2;
	coro::single_consumer_event p3;
	coro::single_consumer_event c1;

	auto produce = [&]() -> coro::async_generator<std::uint32_t> {
		co_await p1;
		co_yield 1;
		co_await p2;
		co_yield 2;
		co_await p3;
	};

	bool consumer_finished = false;

	auto consume = [&]() -> coro::task<> {
		auto generator = produce();
		auto it = co_await generator.begin();
		CHECK(*it == 1u);
		(void)co_await ++it;
		CHECK(*it == 2u);
		co_await c1;
		(void)co_await ++it;
		CHECK(it == generator.end());
		consumer_finished = true;
	};

	auto unblock = [&]() -> coro::task<> {
		p1.set();
		p2.set();
		c1.set();
		CHECK(!consumer_finished);
		p3.set();
		CHECK(consumer_finished);
		co_return;
	};

	coro::sync_wait(coro::when_all_ready(consume(), unblock()));
}

TEST_CASE("exception thrown before first yield is rethrown from begin operation") {
	class TestException {};
	auto gen = [](bool should_throw) -> coro::async_generator<std::uint32_t> {
		if (should_throw) {
			throw TestException();
		}
		co_yield 1;
	}(true);

	coro::sync_wait([&]() -> coro::task<> {
		CHECK_THROWS_AS(co_await gen.begin(), const TestException&);
	}());
}

TEST_CASE("exception thrown after first yield is rethrown from increment operator") {
	class TestException {};
	auto gen = [](bool should_throw) -> coro::async_generator<std::uint32_t> {
		co_yield 1;
		if (should_throw) {
			throw TestException();
		}
	}(true);

	coro::sync_wait([&]() -> coro::task<> {
		auto it = co_await gen.begin();
		CHECK(*it == 1u);
		CHECK_THROWS_AS(co_await ++it, const TestException&);
		CHECK(it == gen.end());
	}());
}

TEST_CASE("large number of synchronous completions doesn't result in stack-overflow") {
	auto make_sequence = [](coro::single_consumer_event& event) -> coro::async_generator<std::uint32_t> {
		for (std::uint32_t i = 0; i < 1'000'000u; ++i) {
			if (i == 500'000u) co_await event;
			co_yield i;
		}
	};

	auto consumer = [](coro::async_generator<std::uint32_t> seq) -> coro::task<> {
		std::uint32_t expected = 0;
		for (auto iter = co_await seq.begin(); iter != seq.end(); co_await ++iter) {
			std::uint32_t i = *iter;
			CHECK(i == expected++);
		}

		CHECK(expected == 1'000'000u);
	};

	auto un_blocker = [](coro::single_consumer_event& event) -> coro::task<> {
		// Should have processed the first 500'000 elements synchronously with consumer driving
		// iteraction before producer suspends and thus consumer suspends.
		// Then we resume producer in call to set() below and it continues processing remaining
		// 500'000 elements, this time with producer driving the interaction.

		event.set();

		co_return;
	};

	coro::single_consumer_event event;

	coro::sync_wait(coro::when_all_ready(consumer(make_sequence(event)), un_blocker(event)));
}

TEST_CASE("fmap") {
	using coro::async_generator;
	using coro::fmap;

	auto iota = [](int count) -> async_generator<int> {
		for (int i = 0; i < count; ++i) {
			co_yield i;
		}
	};

	auto squares = iota(5) | fmap([](auto x) { return x * x; });

	coro::sync_wait([&]() -> coro::task<> {
		auto it = co_await squares.begin();
		CHECK(*it == 0);
		CHECK(*co_await ++it == 1);
		CHECK(*co_await ++it == 4);
		CHECK(*co_await ++it == 9);
		CHECK(*co_await ++it == 16);
		CHECK(co_await ++it == squares.end());
	}());
}

TEST_SUITE_END();
