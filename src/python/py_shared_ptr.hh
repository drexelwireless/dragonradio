// See:
//   https://pybind11.readthedocs.io/en/stable/advanced/cast/custom.html
//   https://github.com/pybind/pybind11/issues/1145
//   https://github.com/pybind/pybind11/issues/1389
//   https://github.com/pybind/pybind11/issues/1546
//
//   https://github.com/pybind/pybind11/pull/1146

namespace pybind11::detail {

template<>
struct type_caster<std::shared_ptr<T>>
{
    PYBIND11_TYPE_CASTER(std::shared_ptr<T>, _(TNAME));

    using BaseCaster = copyable_holder_caster<T, std::shared_ptr<T>>;

    bool load(pybind11::handle src, bool b)
    {
        BaseCaster bc;

        if (!bc.load(src, b))
            return false;

        auto py_obj = py::reinterpret_borrow<py::object>(src);
        auto base_ptr = static_cast<std::shared_ptr<T>>(bc);

        // Construct a shared_ptr to the py::object
        auto py_obj_ptr = std::shared_ptr<object>{
            new object{py_obj},
            [](auto py_object_ptr) {
                // It's possible that when the shared_ptr dies we won't have the
                // gil (if the last holder is in a non-Python thread), so we
                // make sure to acquire it in the deleter.
                gil_scoped_acquire gil;
                delete py_object_ptr;
            }
        };

        value = std::shared_ptr<T>(py_obj_ptr, base_ptr.get());
        return true;
    }

    static handle cast(std::shared_ptr<T> base,
                       return_value_policy rvp,
                       handle h)
    {
        return BaseCaster::cast(base, rvp, h);
    }
};

template <>
struct is_holder_type<T, std::shared_ptr<T>> : std::true_type {};

}
