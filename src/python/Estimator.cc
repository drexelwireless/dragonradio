#include "Estimator.hh"
#include "python/PyModules.hh"

void exportEstimators(py::module &m)
{
    // Export estimator classes to Python
    py::class_<Estimator<float>, std::shared_ptr<Estimator<float>>>(m, "Estimator")
        .def_property_readonly("value", &Estimator<float>::getValue, "The value of the estimator")
        .def_property_readonly("nsamples", &Estimator<float>::getNSamples, "The number of samples used in the estimate")
        .def("reset", &Estimator<float>::reset, "Reset the estimate")
        .def("update", &Estimator<float>::update, "Update the estimate")
        ;

    py::class_<Mean<float>, Estimator<float>, std::shared_ptr<Mean<float>>>(m, "Mean")
        .def(py::init<>())
        .def(py::init<float>())
        .def("remove", &Mean<float>::remove, "Remove a value used to estimate the mean")
        ;
}
