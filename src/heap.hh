#ifndef _HEAP_HH_
#define _HEAP_HH_

#include <functional>
#include <limits>
#include <utility>
#include <vector>

template <class T,
          class Container = std::vector<std::reference_wrapper<T>>,
          class Compare = std::less<T> >
class heap
{
public:
    using container_type = Container;
    using value_compare = Compare;
    using size_type = typename Container::size_type;
    using reference = T&;
    using const_reference = const T&;

    class element {
    public:
        element() : heap_index(std::numeric_limits<size_type>::max()) {}

        bool in_heap() const
        {
            return heap_index != std::numeric_limits<size_type>::max();
        }

        bool is_top() const
        {
            return heap_index == 0;
        }

        size_type heap_index;
    };

    heap(const Compare& compare, const Container& cont)
      : c(cont)
      , comp(compare)
    {
    }

    explicit heap(const Compare& compare = Compare(),
                  Container&& cont = Container())
      : c(std::move(cont))
      , comp(compare)
    {
    }

    ~heap() = default;

    heap& operator=(const heap& other)
    {
        c = other.c;
        comp = other.comp;
    }

    heap& operator=(heap&& other)
    {
        c = std::move(other.c);
        comp = std::move(other.comp);
    }

    reference top()
    {
        return c.front();
    }

    bool empty() const
    {
        return c.empty();
    }

    size_type size() const
    {
        return c.size();
    }

    void push(T& value)
    {
        c.push_back(std::ref(value));
        value.heap_index = size() - 1;
        up_heap(size() - 1);
    }

    void pop()
    {
        swap_heap(0, c.size()-1);
        c[c.size()-1].get().heap_index = std::numeric_limits<size_type>::max();
        c.pop_back();
        down_heap(0);
    }

    void remove(T& value)
    {
        if (value.heap_index == std::numeric_limits<size_type>::max())
            return;

        remove_heap(value.heap_index);
    }

    void update(T& value)
    {
        if (value.heap_index == std::numeric_limits<size_type>::max())
            return;

        update_heap(value.heap_index);
    }

	  void swap(heap& other)
    {
        std::swap(c, other.c);
        std::swap(comp, other.comp);
    }

protected:
    Container c;
    Compare comp;

    static constexpr size_type parent(size_type i)
    {
        return (i-1)/2;
    }

    static constexpr size_type left(size_type i)
    {
        return 2*i + 1;
    }

    static constexpr size_type right(size_type i)
    {
        return 2*i + 2;
    }

    /** @brief Construct a heap from unordered elements */
    void make_heap()
    {
        for (size_type i = parent(c.size() - 1); i != 0; --i)
            down_heap(i);
    }

    /** @brief Remove element at the given index */
    void remove_heap(size_type index)
    {
        swap_heap(index, c.size() - 1);

        c[c.size() - 1].get().heap_index = std::numeric_limits<size_type>::max();
        c.pop_back();

        if (index != c.size())
            update_heap(index);
    }

    /** @brief Move item at given index to proper heap position */
    void update_heap(size_type index)
    {
        if (index > 0 && comp(c[index], c[parent(index)]))
            up_heap(index);
        else
            down_heap(index);
    }

    /** @brief Move item at the given index up the heap to the proper index */
    void up_heap(size_type index)
    {
        while (index > 0) {
            size_type p = parent(index);

            if (!comp(c[index], c[p]))
                break;

            swap_heap(index, p);
            index = p;
        }
    }

    /** @brief Move item at the given index down the heap to the proper index */
    void down_heap(size_type index)
    {
        size_type child = left(index);

        while (child < c.size()) {
            size_type top_child = (child + 1 == c.size() || comp(c[child], c[child + 1])) ? child : child + 1;

            if (comp(c[index], c[top_child]))
                break;

            swap_heap(index, top_child);
            index = top_child;
            child = left(index);
        }
    }

    /** @brief Swap the elements at the given heap indices */
    void swap_heap(size_type index1, size_type index2)
    {
        if (index1 == index2)
            return;

        std::swap(c[index1], c[index2]);
        c[index1].get().heap_index = index1;
        c[index2].get().heap_index = index2;
    }
};

#endif /* _HEAP_HH_ */
