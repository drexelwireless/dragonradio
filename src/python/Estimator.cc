#include "python/PyModules.hh"
#include "stats/Estimator.hh"

template <class T>
void exportEstimator(py::module &m, const char *name)
{
    py::class_<Estimator<T>, std::shared_ptr<Estimator<T>>>(m, name)
        .def_property_readonly("value",
            &Estimator<T>::getValue,
            "The value of the estimator")
        .def_property_readonly("nsamples",
            &Estimator<T>::getNSamples,
            "The number of samples used in the estimate")
        .def("reset",
            &Estimator<T>::reset,
            "Reset the estimate")
        .def("update",
            &Estimator<T>::update,
            "Update the estimate")
        ;
}

template <class T>
void exportMeanEstimator(py::module &m, const char *name)
{
    py::class_<Mean<T>, Estimator<T>, std::shared_ptr<Mean<T>>>(m, name)
        .def(py::init<>())
        .def(py::init<T>())
        .def("remove",
            &Mean<T>::remove,
            "Remove a value used to estimate the mean")
        ;
}

template <class T>
void exportWindowedMeanEstimator(py::module &m, const char *name)
{
    py::class_<WindowedMean<T>, Estimator<T>, std::shared_ptr<WindowedMean<T>>>(m, name)
        .def_property("window_size",
            &WindowedMean<T>::getWindowSize,
            &WindowedMean<T>::setWindowSize,
            "Number of samples in window")
        ;
}

void exportEstimators(py::module &m)
{
    exportEstimator<float>(m, "FloatEstimator");
    exportEstimator<double>(m, "DoubleEstimator");

    exportMeanEstimator<float>(m, "FloatMean");
    exportMeanEstimator<double>(m, "DoubleMean");

    exportWindowedMeanEstimator<float>(m, "FloatWindowedMean");
    exportWindowedMeanEstimator<double>(m, "DoubleWindowedMean");
}
