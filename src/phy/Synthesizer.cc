// Copyright 2022 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "phy/Synthesizer.hh"

void Synthesizer::reconfigure(void)
{
    // Re-enable the sink
    sink.enable();
}

void Synthesizer::wake_dependents(void)
{
    // Disable the sink
    sink.disable();

    sync_barrier::wake_dependents();
}
