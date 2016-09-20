#pragma once

#include <iterator>

namespace std {

enum class byte : unsigned char {};

// [views.constants], constants
constexpr ptrdiff_t dynamic_extent = -1;

// A view over a contiguous, single-dimension sequence of objects
template <class ElementType, ptrdiff_t Extent = dynamic_extent>
class span
{
public:
    // constants and types
    using element_type = ElementType;
    using index_type = ptrdiff_t;
    using pointer = element_type*;
    using reference = element_type&;
    using iterator = pointer;
    using const_iterator = const pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    constexpr static index_type extent = Extent;

    // [span.cons], span constructors, copy, assignment, and destructor constexpr span();
    constexpr span(nullptr_t) : m_data{nullptr}, m_size{0}
    {}

    constexpr span(pointer ptr, index_type count) : m_data{ptr}, m_size{count}
    {}

    constexpr span(pointer firstElem, pointer lastElem) : m_data{firstElem}, m_size{std::distance(firstElem,lastElem)}
    {}

    template <size_t N>
    constexpr span(element_type (&arr)[N]) : m_data{arr}, m_size{N}
    {}

    template <size_t N>
    constexpr span(array<remove_const_t<element_type>, N>& arr) : m_data{arr.data()}, m_size{N}
    {}

    template <size_t N>
    constexpr span(const array<remove_const_t<element_type>, N>& arr) : m_data{arr.data()}, m_size{N}
    {}

    template <class Container>
    constexpr span(Container& cont) : m_data{cont.data()}, m_size{cont.size()}
    {}

    template <class Container> span(const Container&&) = delete;

    constexpr span(const span& other) noexcept = default;

    constexpr span(span&& other) noexcept = default;

    template <class OtherElementType, ptrdiff_t OtherExtent>
    constexpr span(const span<OtherElementType, OtherExtent>& other)
    {
        static_assert("Not implemented");
    }

    template <class OtherElementType, ptrdiff_t OtherExtent>
    constexpr span(span<OtherElementType, OtherExtent>&& other)
    {
        static_assert("Not implemented");
    }

    ~span() noexcept = default;

    constexpr span& operator=(const span& other) noexcept = default;

    constexpr span& operator=(span&& other) noexcept = default;

    // [span.sub], span subviews
    template <ptrdiff_t Count>
    constexpr span<element_type, Count> first() const
    {
        return first(Count);
    }

    template <ptrdiff_t Count>
    constexpr span<element_type, Count> last() const
    {
        return last(Count);
    }

    template <ptrdiff_t Offset, ptrdiff_t Count = dynamic_extent>
    constexpr span<element_type, Count> subspan() const
    {
        return subspan(Offset);
    }

    constexpr span<element_type, dynamic_extent> first(index_type count) const
    {
        if(count > m_size) std::terminate();
        return {m_data,count};
    }

    constexpr span<element_type, dynamic_extent> last(index_type count) const
    {
        if(count > m_size) std::terminate();
        return {m_data+m_size-count,count};
    }

    constexpr span<element_type, dynamic_extent> subspan(index_type offset, index_type count = dynamic_extent) const
    {
        if(offset > m_size) std::terminate();
        return {m_data+offset,m_size-offset};
    }

    // [span.obs], span observers
    constexpr index_type length() const noexcept
    {
        return m_size;
    }

    constexpr index_type size() const noexcept
    {
        return m_size;
    }

    constexpr index_type length_bytes() const noexcept
    {
        static_assert("Not implemented");
        return 0;
    }

    constexpr index_type size_bytes() const noexcept
    {
        static_assert("Not implemented");
        return 0;
    }

    constexpr bool empty() const noexcept
    {
        return !m_size;
    }

    // [span.elem], span element access
    constexpr reference operator[](index_type idx) const
    {
        return m_data[idx];
    }

    constexpr reference operator()(index_type idx) const
    {
        return m_data[idx];
    }

    constexpr pointer data() const noexcept
    {
        return m_data;
    }

    // [span.iter], span iterator support
    iterator begin() const noexcept
    {
        return m_data;
    }

    iterator end() const noexcept
    {
        return m_data + m_size;
    }

    const_iterator cbegin() const noexcept
    {
        return m_data;
    }

    const_iterator cend() const noexcept
    {
        return m_data + m_size;
    }

    reverse_iterator rbegin() const noexcept
    {
        return make_reverse_iterator(end());
    }

    reverse_iterator rend() const noexcept
    {
        return make_reverse_iterator(begin());
    }

    const_reverse_iterator crbegin() const noexcept
    {
        return make_reverse_iterator(end());
    }

    const_reverse_iterator crend() const noexcept
    {
        return make_reverse_iterator(begin());
    }

private:
    // exposition only

    pointer m_data;

    index_type m_size;
};

// [span.comparison], span comparison operators
template <class ElementType, ptrdiff_t Extent>
constexpr bool operator==(const span<ElementType, Extent>& l, const span<ElementType, Extent>& r) noexcept
{
    static_assert("Not implemented");
    return false;
}

template <class ElementType, ptrdiff_t Extent>
constexpr bool operator!=(const span<ElementType, Extent>& l, const span<ElementType, Extent>& r) noexcept
{
    static_assert("Not implemented");
    return false;
}

template <class ElementType, ptrdiff_t Extent>
constexpr bool operator<(const span<ElementType, Extent>& l, const span<ElementType, Extent>& r) noexcept
{
    static_assert("Not implemented");
    return false;
}

template <class ElementType, ptrdiff_t Extent>
constexpr bool operator<=(const span<ElementType, Extent>& l, const span<ElementType, Extent>& r) noexcept
{
    static_assert("Not implemented");
    return false;
}

template <class ElementType, ptrdiff_t Extent>
constexpr bool operator>(const span<ElementType, Extent>& l, const span<ElementType, Extent>& r) noexcept
{
    static_assert("Not implemented");
    return false;
}

template <class ElementType, ptrdiff_t Extent>
constexpr bool operator>=(const span<ElementType, Extent>& l, const span<ElementType, Extent>& r) noexcept
{
    static_assert("Not implemented");
    return false;
}

// [span.objectrep], views of object representation
template <class ElementType, ptrdiff_t Extent>
constexpr span<const byte, ((Extent == dynamic_extent) ? dynamic_extent : (sizeof(ElementType) * Extent))> as_bytes(span<ElementType, Extent> s) noexcept
{
    static_assert("Not implemented");
    return {};
}

template <class ElementType, ptrdiff_t Extent>
constexpr span<byte, ((Extent == dynamic_extent) ? dynamic_extent : (sizeof(ElementType) * Extent))> as_writeable_bytes(span<ElementType, Extent> ) noexcept
{
    static_assert("Not implemented");
    return {};
}

} // namespace std
