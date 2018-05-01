#ifndef BUFFER_H_
#define BUFFER_H_

#include <cstring>
#include <limits>
#include <type_traits>

template <typename T>
class buffer {
#if __cplusplus >= 201703L
    static_assert(std:: is_standard_layout_v<T>, "Buffer can only contain types with a standard layout");
#endif /*  __cplusplus >= 201703 */
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = value_type*;
    using const_iterator = const value_type*;

    buffer() : _data(nullptr), _size(0), _capacity(0) {}

    explicit buffer(size_type count)
    {
        _data = reinterpret_cast<T*>(malloc(count*sizeof(T)));
        if (!_data)
            throw std::bad_alloc();

        _size = count;
        _capacity = count;
    }

    explicit buffer(const T *data, size_type count)
    {
        _data = reinterpret_cast<T*>(malloc(count*sizeof(T)));
        if (!_data)
            throw std::bad_alloc();

        memcpy(_data, data, count*sizeof(T));
        _size = count;
        _capacity = count;
    }

    buffer(const buffer& other)
    {
        _data = reinterpret_cast<T*>(malloc(other._size*sizeof(T)));
        if (!_data)
            throw std::bad_alloc();

        std::memcpy(_data, other._data, other._size*sizeof(T));

        _size = other._size;
        _capacity = other._size;
    }

    buffer(buffer&& other) noexcept
    {
        _data = other._data;
        _size = other._size;
        _capacity = other._size;

        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;
    }

    ~buffer()
    {
        if (_data)
            free(_data);
    }

    buffer& operator=(const buffer& other)
    {
        _data = reinterpret_cast<T*>(malloc(other._size*sizeof(T)));
        if (!_data)
            throw std::bad_alloc();

        std::memcpy(_data, other._data, other._size*sizeof(T));

        _size = other._size;
        _capacity = other._size;

        return *this;
    }

    buffer& operator=(buffer&& other) noexcept
    {
        _data = other._data;
        _size = other._size;
        _capacity = other._size;

        other._data = nullptr;
        other._size = 0;
        other._capacity = 0;

        return *this;
    }

    reference at(size_type pos)
    {
        if (pos >= _size)
            throw std::out_of_range("buffer range check");

        return _data[pos];
    }

    const_reference at(size_type pos) const
    {
        if (pos >= _size)
            throw std::out_of_range("buffer range check");

        return _data[pos];
    }

    reference operator [](size_type pos)
    {
        return _data[pos];
    }

    const_reference operator [](size_type pos) const
    {
        return _data[pos];
    }

    reference front(void)
    {
        return _data[0];
    }

    const_reference front(void) const
    {
        return _data[0];
    }

    reference back(void)
    {
        return _data[_size-1];
    }

    const_reference back(void) const
    {
        return _data[_size-1];
    }

    value_type* data() noexcept
    {
        return _data;
    }

    const value_type* data() const noexcept
    {
        return _data;
    }

    iterator begin(void) noexcept
    {
        return _data;
    }

    const_iterator begin(void) const noexcept
    {
        return _data;
    }

    const_iterator cbegin(void) const noexcept
    {
        return _data;
    }

    iterator end(void) noexcept
    {
        return _data + _size;
    }

    const_iterator end(void) const noexcept
    {
        return _data + _size;
    }

    const_iterator cend(void) const noexcept
    {
        return _data + _size;
    }

    bool empty(void) const noexcept
    {
        return _size == 0;
    }

    size_type size(void) const noexcept
    {
        return _size;
    }

    size_type max_size(void) const noexcept
    {
        return std::numeric_limits<size_type>::max()/sizeof(T);
    }

    void reserve(size_type new_cap)
    {
        if (new_cap > _capacity) {
            size_type _new_capacity = _capacity;

            while (_new_capacity < new_cap)
                _new_capacity *= 2;

            T* new_data = reinterpret_cast<T*>(realloc(_data, _new_capacity*sizeof(T)));
            if (!new_data)
                throw std::bad_alloc();

            _data = new_data;
            _capacity = _new_capacity;
        }
    }

    size_type capacity(void) const noexcept
    {
        return _capacity;
    }

    void shrink_to_fit(void)
    {
        T* new_data = reinterpret_cast<T*>(realloc(_data, _size*sizeof(T)));
        if (!new_data)
            throw std::bad_alloc();

        _data = new_data;
        _capacity = _size;
    }

    void clear(void) noexcept
    {
        _size = 0;
    }

    void push_back(const T& value)
    {
        reserve(_size+1);
        _data[_size] = value;
        ++_size;
    }

    void push_back(T&& value)
    {
        reserve(_size+1);
        new (&_data[_size]) T(std::move(value));
        ++_size;
    }

    template< class... Args >
    reference emplace_back(Args&&... args)
    {
        reserve(_size+1);
        new (&_data[_size]) T(std::forward<Args>(args)...);
        ++_size;
    }

    void pop_back(void)
    {
        --_size;
    }

    void resize(size_type count)
    {
        reserve(count);
        _size = count;
    }

    void swap(buffer<T>& other)
    {
        std::swap(_data, other._data);
        std::swap(_size, other._size);
        std::swap(_capacity, other._capacity);
    }

private:
    T* _data;
    size_t _size;
    size_t _capacity;
};

template<typename T>
bool operator==(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return lhs._size == rhs.size()
           && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template<typename T>
bool operator<(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return std::lexicographical_compare(lhs.begin(), lhs.end(),
                                        rhs.begin(), rhs.end());
}

template<typename T>
bool operator!=(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return !(lhs == rhs);
}

template<typename T>
bool operator>(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return rhs < lhs;
}

template<typename T>
bool operator<=(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return !(rhs < lhs);
}

template<typename T>
bool operator>=(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return !(lhs < rhs);
}

template<typename T>
void swap(buffer<T>& lhs, buffer<T>& rhs)
{
    lhs.swap(rhs);
}

#endif /* BUFFER_H_ */
