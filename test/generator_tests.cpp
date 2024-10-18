//
// Created by Jesson on 2024/10/18.
//

#include <coro/types/generator.h>
#include <coro/on_scope_exit.h>
#include <coro/fmap.h>

#include <iostream>
#include <vector>
#include <string>
#include <forward_list>

#include "doctest/doctest.h"

TEST_SUITE_BEGIN("generator");

using coro::generator;

TEST_CASE("default-constructed generator is empty sequence") {
	generator<int> ints;
	CHECK(ints.begin() == ints.end());
}

TEST_CASE("generator of arithmetic type returns by copy") {
	auto f = []() -> generator<float> {
		co_yield 1.0f;
		co_yield 2.0f;
	};

	auto gen = f();
	auto iter = gen.begin();
	// TODO: Should this really be required?
	//static_assert(std::is_same<decltype(*iter), float>::value, "operator* should return float by value");
	CHECK(*iter == 1.0f);
	++iter;
	CHECK(*iter == 2.0f);
	++iter;
	CHECK(iter == gen.end());
}

TEST_CASE("generator of reference returns by reference") {
	auto f = [](float& value) -> generator<float&> {
		co_yield value;
	};

	float value = 1.0f;
	for (auto& x : f(value)) {
		CHECK(&x == &value);
		x += 1.0f;
	}

	CHECK(value == 2.0f);
}

TEST_CASE("generator of const type") {
	auto fib = []() -> generator<const std::uint64_t> {
		std::uint64_t a = 0, b = 1;
		while (true) {
			co_yield b;
			b += std::exchange(a, b);
		}
	};

	std::uint64_t count = 0;
	for (auto i : fib()) {
		if (i > 1'000'000) {
			break;
		}
		++count;
	}

	// 30th fib number is 832'040
	CHECK(count == 30);
}

TEST_CASE("value-category of fmap() matches reference type") {
    using coro::fmap;

    auto check_is_rvalue = []<typename T>(T&& x) {
        static_assert(std::is_rvalue_reference_v<decltype(x)>);
        static_assert(!std::is_const_v<std::remove_reference_t<decltype(x)>>);
        CHECK(x == 123);
        return x;
    };
    auto check_is_lvalue = []<typename T>(T&& x) {
        static_assert(std::is_lvalue_reference_v<decltype(x)>);
        static_assert(!std::is_const_v<std::remove_reference_t<decltype(x)>>);
        CHECK(x == 123);
        return x;
    };
    auto check_is_const_lvalue = []<typename T>(T&& x) {
        static_assert(std::is_lvalue_reference_v<decltype(x)>);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
        CHECK(x == 123);
        return x;
    };
    auto check_is_const_rvalue = []<typename T>(T&& x) {
        static_assert(std::is_rvalue_reference_v<decltype(x)>);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(x)>>);
        CHECK(x == 123);
        return x;
    };

    auto consume = [](auto&& range) {
        for (auto&& x : range) {
            (void)x;
        }
    };

    consume([]() -> generator<int> { co_yield 123; }() | fmap(check_is_lvalue));
    consume([]() -> generator<const int> { co_yield 123; }() | fmap(check_is_const_lvalue));
    consume([]() -> generator<int&> { co_yield 123; }() | fmap(check_is_lvalue));
    consume([]() -> generator<const int&> { co_yield 123; }() | fmap(check_is_const_lvalue));
    consume([]() -> generator<int&&> { co_yield 123; }() | fmap(check_is_rvalue));
    consume([]() -> generator<const int&&> { co_yield 123; }() | fmap(check_is_const_rvalue));
}

TEST_CASE("generator doesn't start until its called") {
	bool reached1 = false;
	bool reached2 = false;
	bool reached3 = false;
	auto f = [&]() -> generator<int> {
		reached1 = true;
		co_yield 1;
		reached2 = true;
		co_yield 2;
		reached3 = true;
	};

	auto gen = f();
	CHECK(!reached1);
	auto iter = gen.begin();
	CHECK(reached1);
	CHECK(!reached2);
	CHECK(*iter == 1);
	++iter;
	CHECK(reached2);
	CHECK(!reached3);
	CHECK(*iter == 2);
	++iter;
	CHECK(reached3);
	CHECK(iter == gen.end());
}

TEST_CASE("destroying generator before completion destructs objects on stack") {
	bool destructed = false;
	bool completed = false;

    {
        auto f = [&]() -> generator<int> {
            auto onExit = coro::on_scope_exit([&] { destructed = true; });
            co_yield 1;
            co_yield 2;
            completed = true;
        };
        auto g = f();
		auto it = g.begin();
		auto itEnd = g.end();
		CHECK(it != itEnd);
		CHECK(*it == 1u);
		CHECK(!destructed);
	}

	CHECK(!completed);
	CHECK(destructed);
}

TEST_CASE("generator throwing before yielding first element rethrows out of begin()") {
	class X {};

	auto g = []() -> generator<int> {
		throw X{};
		co_return;
	}();

	try {
		g.begin();
		FAIL("should have thrown");
	}
	catch (const X&) {}
}

TEST_CASE("generator throwing after first element rethrows out of operator++") {
	class X {};

	auto g = []() -> coro::generator<int> {
		co_yield 1;
		throw X{};
	}();

	auto iter = g.begin();
	REQUIRE(iter != g.end());
	try {
		++iter;
		FAIL("should have thrown");
	}
	catch (const X&) {}
}

namespace {
	template<typename FIRST, typename SECOND>
	auto concat(FIRST&& first, SECOND&& second) {
		using value_type = std::remove_reference_t<decltype(*first.begin())>;
		return [](FIRST first, SECOND second) -> coro::generator<value_type> {
			for (auto&& x : first) co_yield x;
			for (auto&& y : second) co_yield y;
		}(std::forward<FIRST>(first), std::forward<SECOND>(second));
	}
}

TEST_CASE("safe capture of r-value reference args") {
	using namespace std::string_literals;

	// Check that we can capture l-values by reference and that temporary
	// values are moved into the coroutine frame.
	std::string by_ref = "bar";
	auto g = concat("foo"s, concat(by_ref, std::vector<char>{ 'b', 'a', 'z' }));

	by_ref = "buzz";

	std::string s;
	for (char c : g) {
		s += c;
	}

	CHECK(s == "foobuzzbaz");
}

namespace {
	coro::generator<int> range(int start, int end) {
		for (; start < end; ++start) {
			co_yield start;
		}
	}
}

TEST_CASE("fmap operator") {
	coro::generator<int> gen = range(0, 5) | coro::fmap([](int x) { return x * 3; });

	auto it = gen.begin();
	CHECK(*it == 0);
	CHECK(*++it == 3);
	CHECK(*++it == 6);
	CHECK(*++it == 9);
	CHECK(*++it == 12);
	CHECK(++it == gen.end());
}

namespace {
	template<std::size_t window, typename Range>
	coro::generator<const double> low_pass(Range rng) {
		auto it = std::begin(rng);
		const auto it_end = std::end(rng);

		const double inv_count = 1.0 / window;
		double sum = 0;

		using iter_cat = typename std::iterator_traits<decltype(it)>::iterator_category;

		if constexpr (std::is_base_of_v<std::random_access_iterator_tag, iter_cat>) {
			for (std::size_t count = 0; it != it_end && count < window; ++it) {
				sum += *it;
				++count;
				co_yield sum / count;
			}

			for (; it != it_end; ++it) {
				sum -= *(it - window);
				sum += *it;
				co_yield sum * inv_count;
			}
		}
		else if constexpr (std::is_base_of_v<std::forward_iterator_tag, iter_cat>) {
			auto window_start = it;
			for (std::size_t count = 0; it != it_end && count < window; ++it) {
				sum += *it;
				++count;
				co_yield sum / count;
			}

			for (; it != it_end; ++it, ++window_start) {
				sum -= *window_start;
				sum += *it;
				co_yield sum * inv_count;
			}
		}
		else {
			// Just assume an input iterator
			double buffer[window];

			for (std::size_t count = 0; it != it_end && count < window; ++it) {
				buffer[count] = *it;
				sum += buffer[count];
				++count;
				co_yield sum / count;
			}

			for (std::size_t pos = 0; it != it_end; ++it, pos = (pos + 1 == window) ? 0 : (pos + 1)) {
				sum -= std::exchange(buffer[pos], *it);
				sum += buffer[pos];
				co_yield sum * inv_count;
			}
		}
	}
}

TEST_CASE("low_pass") {
	// With random-access iterator
	{
		auto gen = low_pass<4>(std::vector<int>{ 10, 13, 10, 15, 18, 9, 11, 15 });
		auto it = gen.begin();
		CHECK(*it == 10.0);
		CHECK(*++it == 11.5);
		CHECK(*++it == 11.0);
		CHECK(*++it == 12.0);
		CHECK(*++it == 14.0);
		CHECK(*++it == 13.0);
		CHECK(*++it == 13.25);
		CHECK(*++it == 13.25);
		CHECK(++it == gen.end());
	}

	// With forward-iterator
	{
		auto gen = low_pass<4>(std::forward_list<int>{ 10, 13, 10, 15, 18, 9, 11, 15 });
		auto it = gen.begin();
		CHECK(*it == 10.0);
		CHECK(*++it == 11.5);
		CHECK(*++it == 11.0);
		CHECK(*++it == 12.0);
		CHECK(*++it == 14.0);
		CHECK(*++it == 13.0);
		CHECK(*++it == 13.25);
		CHECK(*++it == 13.25);
		CHECK(++it == gen.end());
	}

	// With input-iterator
	{
		auto gen = low_pass<3>(range(10, 20));
		auto it = gen.begin();
		CHECK(*it == 10.0);
		CHECK(*++it == 10.5);
		CHECK(*++it == 11.0);
		CHECK(*++it == 12.0);
		CHECK(*++it == 13.0);
		CHECK(*++it == 14.0);
		CHECK(*++it == 15.0);
		CHECK(*++it == 16.0);
		CHECK(*++it == 17.0);
		CHECK(*++it == 18.0);
		CHECK(++it == gen.end());
	}
}

TEST_SUITE_END();
