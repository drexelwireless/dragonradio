#ifndef WRAPPER_HH_
#define WRAPPER_HH_

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

template <typename T>
py::array_t<T> wrapper(std::vector<T> &&input) {
    std::vector<T> *result = new std::vector<T>(input);

    py::capsule free_when_done(result, [](void *ptr) {
        auto result = reinterpret_cast<std::vector<T>*>(ptr);
        delete result;
    });

    return py::array_t<T>({result->size()}, // shape
                          {sizeof(T)},      // stride
                          result->data(),   // data pointer
                          free_when_done);
}

#endif /* WRAPPER_HH_ */