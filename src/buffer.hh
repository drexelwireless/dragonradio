#ifndef BUFFER_H_
#define BUFFER_H_

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

template <typename T>
class buffer {
    static_assert(std::is_standard_layout<T>::value, "Buffer can only contain types with a standard layout");
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

    buffer() : data_(nullptr), size_(0), capacity_(0) {}

    explicit buffer(size_type count)
    {
        data_ = reinterpret_cast<T*>(std::malloc(count*sizeof(T)));
        if (!data_)
            throw std::bad_alloc();

        size_ = count;
        capacity_ = count;
    }

    explicit buffer(const T *data, size_type count)
    {
        data_ = reinterpret_cast<T*>(std::malloc(count*sizeof(T)));
        if (!data_)
            throw std::bad_alloc();

        memcpy(data_, data, count*sizeof(T));
        size_ = count;
        capacity_ = count;
    }

    buffer(const buffer& other)
    {
        data_ = reinterpret_cast<T*>(std::malloc(other.size_*sizeof(T)));
        if (!data_)
            throw std::bad_alloc();

        std::memcpy(data_, other.data_, other.size_*sizeof(T));

        size_ = other.size_;
        capacity_ = other.size_;
    }

    buffer(buffer&& other) noexcept
    {
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.size_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ~buffer()
    {
        if (data_)
            free(data_);
    }

    buffer& operator=(const buffer& other)
    {
        data_ = reinterpret_cast<T*>(std::malloc(other.size_*sizeof(T)));
        if (!data_)
            throw std::bad_alloc();

        std::memcpy(data_, other.data_, other.size_*sizeof(T));

        size_ = other.size_;
        capacity_ = other.size_;

        return *this;
    }

    buffer& operator=(buffer&& other) noexcept
    {
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.size_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;

        return *this;
    }

    reference at(size_type pos)
    {
        if (pos >= size_)
            throw std::out_of_range("buffer range check");

        return data_[pos];
    }

    const_reference at(size_type pos) const
    {
        if (pos >= size_)
            throw std::out_of_range("buffer range check");

        return data_[pos];
    }

    reference operator [](size_type pos)
    {
        return data_[pos];
    }

    const_reference operator [](size_type pos) const
    {
        return data_[pos];
    }

    reference front(void)
    {
        return data_[0];
    }

    const_reference front(void) const
    {
        return data_[0];
    }

    reference back(void)
    {
        return data_[size_-1];
    }

    const_reference back(void) const
    {
        return data_[size_-1];
    }

    value_type* data() noexcept
    {
        return data_;
    }

    const value_type* data() const noexcept
    {
        return data_;
    }

    iterator begin(void) noexcept
    {
        return data_;
    }

    const_iterator begin(void) const noexcept
    {
        return data_;
    }

    const_iterator cbegin(void) const noexcept
    {
        return data_;
    }

    iterator end(void) noexcept
    {
        return data_ + size_;
    }

    const_iterator end(void) const noexcept
    {
        return data_ + size_;
    }

    const_iterator cend(void) const noexcept
    {
        return data_ + size_;
    }

    bool empty(void) const noexcept
    {
        return size_ == 0;
    }

    size_type size(void) const noexcept
    {
        return size_;
    }

    size_type max_size(void) const noexcept
    {
        return std::numeric_limits<size_type>::max()/sizeof(T);
    }

    void reserve(size_type new_cap)
    {
        if (new_cap > capacity_) {
            size_type new_capacity = capacity_;

            if (new_capacity == 0) {
                new_capacity = new_cap;
            } else {
                while (new_capacity < new_cap)
                    new_capacity *= 2;
            }

            T* new_data = reinterpret_cast<T*>(std::realloc(data_, new_capacity*sizeof(T)));
            if (!new_data)
                throw std::bad_alloc();

            data_ = new_data;
            capacity_ = new_capacity;
        }
    }

    size_type capacity(void) const noexcept
    {
        return capacity_;
    }

    void shrink_to_fit(void)
    {
        T* new_data = reinterpret_cast<T*>(std::realloc(data_, size_*sizeof(T)));
        if (!new_data)
            throw std::bad_alloc();

        data_ = new_data;
        capacity_ = size_;
    }

    void clear(void) noexcept
    {
        size_ = 0;
    }

    void push_back(const T& value)
    {
        reserve(size_+1);
        data_[size_] = value;
        ++size_;
    }

    void push_back(T&& value)
    {
        reserve(size_+1);
        new (&data_[size_]) T(std::move(value));
        ++size_;
    }

    template< class... Args >
    reference emplace_back(Args&&... args)
    {
        reserve(size_+1);
        new (&data_[size_]) T(std::forward<Args>(args)...);
        ++size_;
    }

    void pop_back(void)
    {
        --size_;
    }

    void resize(size_type count)
    {
        reserve(count);
        size_ = count;
    }

    void swap(buffer<T>& other)
    {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    void append(size_type count)
    {
        size_t n = size_;

        resize(n + count);
        memset(reinterpret_cast<void*>(&data_[n]), 0, count*sizeof(T));
    }

private:
    T* data_;
    size_t size_;
    size_t capacity_;
};

template<typename T>
bool operator==(const buffer<T>& lhs, const buffer<T>& rhs)
{
    return lhs.size_ == rhs.size()
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
