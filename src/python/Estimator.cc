// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "Clock.hh"
#include "python/PyModules.hh"
#include "stats/Estimator.hh"
#include "stats/TimeWindowEstimator.hh"

template <class T>
void exportEstimator(py::module &m, const char *name)
{
    py::class_<Estimator<T>, std::shared_ptr<Estimator<T>>>(m, name)
        .def_property_readonly("value",
            &Estimator<T>::value,
            "The value of the estimator")
        .def_property_readonly("size",
            &Estimator<T>::size,
            "The number of samples used in the estimate")
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
        .def("reset",
            &Mean<T>::reset,
            "Reset the estimate")
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
        .def("reset",
            &WindowedMean<T>::reset,
            "Reset the estimate")
        ;
}

template <class Clock, class T>
void exportTimeWindowEstimator(py::module &m, const char *name)
{
    py::class_<TimeWindowEstimator<Clock, T>, Estimator<T>, std::shared_ptr<TimeWindowEstimator<Clock, T>>>(m, name)
        .def_property("time_window",
            &TimeWindowEstimator<Clock, T>::getTimeWindow,
            &TimeWindowEstimator<Clock, T>::setTimeWindow,
            "The time window (sec)")
        .def_property_readonly("start",
            [](TimeWindowEstimator<Clock, T> &self) -> std::optional<typename Clock::time_point>
            {
                return self.getTimeWindowStart();
            },
            "The start of the time window (sec)")
        .def_property_readonly("end",
            [](TimeWindowEstimator<Clock, T> &self) -> std::optional<typename Clock::time_point>
            {
                return self.getTimeWindowEnd();
            },
            "The end of the time window (sec)")
        .def("reset",
            &TimeWindowEstimator<Clock, T>::reset,
            "Reset the estimate")
        ;
}

template <class Clock, class T>
void exportTimeWindowMeanEstimator(py::module &m, const char *name)
{
    py::class_<TimeWindowMean<Clock, T>, TimeWindowEstimator<Clock, T>, std::shared_ptr<TimeWindowMean<Clock, T>>>(m, name)
        ;
}

template <class Clock, class T>
void exportTimeWindowMeanRateEstimator(py::module &m, const char *name)
{
    py::class_<TimeWindowMeanRate<Clock, T>, TimeWindowMean<Clock, T>, std::shared_ptr<TimeWindowMeanRate<Clock, T>>>(m, name)
        ;
}

template <class Clock, class T>
void exportTimeWindowMinEstimator(py::module &m, const char *name)
{
    py::class_<TimeWindowMin<Clock, T>, TimeWindowEstimator<Clock, T>, std::shared_ptr<TimeWindowMin<Clock, T>>>(m, name)
        ;
}

template <class Clock, class T>
void exportTimeWindowMaxEstimator(py::module &m, const char *name)
{
    py::class_<TimeWindowMax<Clock, T>, TimeWindowEstimator<Clock, T>, std::shared_ptr<TimeWindowMax<Clock, T>>>(m, name)
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

    exportTimeWindowEstimator<WallClock, double>(m, "WallTimeWindowEstimator");
    exportTimeWindowMeanEstimator<WallClock, double>(m, "WallTimeWindowMean");
    exportTimeWindowMeanRateEstimator<WallClock, double>(m, "WallTimeWindowMeanRate");
    exportTimeWindowMinEstimator<WallClock, double>(m, "WallTimeWindowMin");
    exportTimeWindowMaxEstimator<WallClock, double>(m, "WallTimeWindowMax");

    exportTimeWindowEstimator<MonoClock, double>(m, "MonoTimeWindowEstimator");
    exportTimeWindowMeanEstimator<MonoClock, double>(m, "MonoTimeWindowMean");
    exportTimeWindowMeanRateEstimator<MonoClock, double>(m, "MonoTimeWindowMeanRate");
    exportTimeWindowMinEstimator<MonoClock, double>(m, "MonoTimeWindowMin");
    exportTimeWindowMaxEstimator<MonoClock, double>(m, "MonoTimeWindowMax");
}
