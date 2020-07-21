#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "cil/CIL.hh"
#include "cil/Scorer.hh"
#include "python/PyModules.hh"

PYBIND11_MAKE_OPAQUE(MandateMap)

void exportCIL(py::module &m)
{
    // Export Mandate class to Python
    py::class_<Mandate, std::unique_ptr<Mandate>>(m, "Mandate")
    .def(py::init<FlowUID,
                  double,
                  int,
                  std::optional<double>,
                  std::optional<double>,
                  std::optional<double>>())
    .def_readonly("flow_uid",
        &Mandate::flow_uid,
        "Flow UID)")
    .def_readonly("hold_period",
        &Mandate::hold_period,
        "Steady state period required for mandate success (sec)")
    .def_readonly("point_value",
        &Mandate::point_value,
        "Point value")
    .def_readonly("max_latency_s",
        &Mandate::max_latency_s,
        "Maximum latency allowed for a packet (sec)")
    .def_readonly("min_throughput_bps",
        &Mandate::min_throughput_bps,
        "Minimum throughput (bps)")
    .def_readonly("file_transfer_deadline_s",
        &Mandate::file_transfer_deadline_s,
        "File transfer delivery deadline (sec)")
    .def_readwrite("achieved_duration",
        &Mandate::achieved_duration,
        "Achieved duration")
    .def_readwrite("scalar_performance",
        &Mandate::scalar_performace,
        "Scalar performance")
    .def_readonly("mandated_latency",
        &Mandate::mandated_latency,
        "Maximum latency (sec)")
    .def_readwrite("radio_ids",
        &Mandate::radio_ids,
        "Nodes in flow")
    ;

    py::bind_map<MandateMap>(m, "MandateMap");

    // Export Score class to Python
    py::class_<Score, std::unique_ptr<Score>>(m, "Score")
    .def_readonly("npackets_sent",
        &Score::npackets_sent,
        "Number of packets sent")
    .def_readonly("nbytes_sent",
        &Score::nbytes_sent,
        "Number of bytes sent")
    .def_readonly("update_timestamp_sent",
        &Score::update_timestamp_sent,
        "Timestamp of last update for send statistics")
    .def_readonly("npackets_recv",
        &Score::npackets_recv,
        "Number of packets sent")
    .def_readonly("nbytes_recv",
        &Score::nbytes_recv,
        "Number of bytes sent")
    .def_readonly("update_timestamp_recv",
        &Score::update_timestamp_recv,
        "Timestamp of last update for receive statistics")
    .def_readonly("goal",
        &Score::goal,
        "Was goal met in MP?")
    .def_readonly("goal_stable",
        &Score::goal_stable,
        "Was goal stable in MP?")
    .def_readonly("achieved_duration",
        &Score::achieved_duration,
        "Number of consecutive MP's in which goal has been met")
    .def_readonly("mp_score",
        &Score::mp_score,
        "Score for this MP")
    ;

    py::bind_vector<Scores>(m, "Scores")
    .def_readonly("invalid_mp",
        &Scores::invalid_mp,
        "First invalid MP")
    ;

    py::bind_map<ScoreMap>(m, "ScoreMap");

    // Export Scorer class to Python
    py::class_<Scorer, std::unique_ptr<Scorer>>(m, "Scorer")
    .def(py::init())
    .def("setMandates",
        &Scorer::setMandates,
        "Set mandates")
    .def_property_readonly("scores",
        &Scorer::getScores,
        "Scores")
    .def("updateSentStatistics",
        &Scorer::updateSentStatistics,
        "Update statistics for sent data")
    .def("updateReceivedStatistics",
        &Scorer::updateReceivedStatistics,
        "Update statistics for received data")
    .def("updateScore",
        &Scorer::updateScore,
        "Update scores up to given MP")
    ;
}
