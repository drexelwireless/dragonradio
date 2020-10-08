#include <time.h>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <fstream>

#include "mgen.hh"
#include "wrapper.hh"

namespace py = pybind11;

PYBIND11_MODULE(_dragonradio_tools_mgen, m) {
    PYBIND11_NUMPY_DTYPE(Send,
        timestamp,
        flow,
        seq,
        frag,
        tos,
        src_port,
        dest_ip,
        dest_port,
        size);

    PYBIND11_NUMPY_DTYPE(Recv,
        timestamp,
        flow,
        seq,
        frag,
        tos,
        src_ip,
        src_port,
        dest_ip,
        dest_port,
        sent,
        size);

    m.def("parseSend",
        [](const char *path)
        {
            return wrapper(std::move(parseSend(path)));
        },
        "Parse MGEN SEND records")
    ;

    m.def("parseRecv",
        [](const char *path)
        {
            return wrapper(std::move(parseRecv(path)));
        },
        "Parse MGEN RECV records")
    ;

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}

#if 0

    npy_format_descriptor<datetime64ns,void>::register_dtype();

template <>
struct pybind11::detail::npy_format_descriptor<datetime64ns> {
    static constexpr auto name = _("datetime64[ns]");

    static pybind11::dtype dtype() {
        return py::dtype("datetime64[ns]");
    }
};

template <typename T, typename SFINAE> struct npy_format_descriptor {
    static_assert(pybind11::detail::is_pod_struct<T>::value, "Attempt to use a non-POD or unimplemented POD type as a numpy dtype");

    static constexpr auto name = pybind11::detail::make_caster<T>::name;

    static pybind11::dtype dtype() {
        return pybind11::reinterpret_borrow<pybind11::dtype>(dtype_ptr());
    }

    static std::string format() {
        static auto format_str = pybind11::detail::get_numpy_internals().get_type_info<T>(true)->format_str;
        return format_str;
    }

    static void register_dtype(void) {
        const std::type_info& tinfo = typeid(typename std::remove_cv<T>::type);
        ssize_t itemsize = sizeof(T);

        auto& numpy_internals = pybind11::detail::get_numpy_internals();
        if (numpy_internals.get_type_info(tinfo, false))
            pybind11::pybind11_fail("NumPy: dtype is already registered");

        auto dtype_ptr = py::dtype("datetime64[ns]").release().ptr();

        auto tindex = std::type_index(tinfo);
        numpy_internals.registered_dtypes[tindex] = { dtype_ptr, "datetime64[ns]" };
        pybind11::detail::get_internals().direct_conversions[tindex].push_back(&direct_converter);
    }

private:
    static PyObject* dtype_ptr() {
        static PyObject* ptr = pybind11::detail::get_numpy_internals().get_type_info<T>(true)->dtype_ptr;
        return ptr;
    }

    static bool direct_converter(PyObject *obj, void*& value) {
        auto& api = pybind11::detail::npy_api::get();
        if (!PyObject_TypeCheck(obj, api.PyVoidArrType_Type_))
            return false;
        if (auto descr = pybind11::reinterpret_steal<pybind11::object>(api.PyArray_DescrFromScalar_(obj))) {
            if (api.PyArray_EquivTypes_(dtype_ptr(), descr.ptr())) {
                value = ((pybind11::detail::PyVoidScalarObject_Proxy *) obj)->obval;
                return true;
            }
        }
        return false;
    }
};
#endif
