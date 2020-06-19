#ifndef CAPSULE_H_
#define CAPSULE_H_

#include <pybind11/pybind11.h>

namespace py = pybind11;

/** @brief Save a shared_ptr in a capsule */
template <class T>
py::capsule sharedptr_capsule(const std::shared_ptr<T> &ptr)
{
    auto cpptr = new std::shared_ptr<T>(ptr);

    return py::capsule(cpptr, [](void *cpptr) { delete reinterpret_cast<std::shared_ptr<T>*>(cpptr); });
}

#endif /* CAPSULE_H_ */
