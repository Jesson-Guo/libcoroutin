//
// Created by Jesson on 2024/10/16.
//

#ifndef SEQUENCE_RANGE_H
#define SEQUENCE_RANGE_H

#include "sequence_traits.h"

#include <algorithm>
#include <cassert>

namespace coro {

template<typename sequence, typename traits = sequence_traits<sequence>>
class sequence_range {
public:
    using value_type = sequence;
    using difference_type = typename traits::difference_type;
    using size_type = typename traits::size_type;
    using const_reference = const sequence&;

    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = sequence;
        using difference_type = typename traits::difference_type;
        using reference = const sequence&;
        using pointer = const sequence*;

        explicit constexpr const_iterator(sequence value) noexcept : m_value(value) {}

        const_reference operator*() const noexcept { return m_value; }
        pointer operator->() const noexcept { return std::addressof(m_value); }

        // 前置递增/递减
        const_iterator& operator++() noexcept { ++m_value; return *this; }
        const_iterator& operator--() noexcept { --m_value; return *this; }

        // 后置递增/递减
        const_iterator operator++(int) noexcept { const_iterator temp = *this; ++(*this); return temp; }
        const_iterator operator--(int) noexcept { const_iterator temp = *this; --(*this); return temp; }

        // 复合赋值运算符
        const_iterator& operator+=(difference_type n) noexcept { m_value = static_cast<sequence>(m_value + n); return *this; }
        const_iterator& operator-=(difference_type n) noexcept { m_value = static_cast<sequence>(m_value - n); return *this; }

        // 算术运算符
        friend constexpr const_iterator operator+(const const_iterator& it, difference_type n) noexcept { return const_iterator{ static_cast<sequence>(it.m_value + n) }; }
        friend constexpr const_iterator operator+(difference_type n, const const_iterator& it) noexcept { return const_iterator{ static_cast<sequence>(it.m_value + n) }; }
        friend constexpr const_iterator operator-(const const_iterator& it, difference_type n) noexcept { return const_iterator{ static_cast<sequence>(it.m_value - n) }; }
        constexpr difference_type operator-(const const_iterator& other) const noexcept { return traits::difference(m_value, other.m_value); }

        // 比较运算符
        constexpr bool operator==(const const_iterator& other) const noexcept { return m_value == other.m_value; }
        constexpr bool operator!=(const const_iterator& other) const noexcept { return !(*this == other); }
        constexpr bool operator<(const const_iterator& other) const noexcept { return m_value < other.m_value; }
        constexpr bool operator>(const const_iterator& other) const noexcept { return other < *this; }
        constexpr bool operator<=(const const_iterator& other) const noexcept { return !(other < *this); }
        constexpr bool operator>=(const const_iterator& other) const noexcept { return !(*this < other); }

    private:
        sequence m_value;
    };

    constexpr sequence_range() noexcept
        : m_begin()
        , m_end() {}

    constexpr sequence_range(sequence begin, sequence end) noexcept
        : m_begin(begin)
        , m_end(end) {
        // 确保范围合法
        assert(!traits::precedes(m_end, m_begin));
    }

    constexpr const_iterator begin() const noexcept { return const_iterator(m_begin); }
    constexpr const_iterator end() const noexcept { return const_iterator(m_end); }

    constexpr const_reference front() const noexcept {
        assert(!empty());
        return m_begin;
    }

    constexpr const_reference back() const noexcept {
        assert(!empty());
        return static_cast<sequence>(m_end - 1);
    }

    constexpr size_type size() const noexcept {
        return static_cast<size_type>(traits::difference(m_end, m_begin));
    }

    constexpr bool empty() const noexcept {
        return m_begin == m_end;
    }

    constexpr const_reference operator[](size_type index) const noexcept {
        assert(index < size());
        return static_cast<sequence>(m_begin + index);
    }

    constexpr sequence_range first(size_type count) const noexcept {
        size_type n = std::min(size(), count);
        return sequence_range{ m_begin, static_cast<sequence>(m_begin + n) };
    }

    constexpr sequence_range skip(size_type count) const noexcept {
        size_type n = std::min(size(), count);
        return sequence_range{ static_cast<sequence>(m_begin + n), m_end };
    }

private:
    sequence m_begin;
    sequence m_end;
};

}

#endif //SEQUENCE_RANGE_H
