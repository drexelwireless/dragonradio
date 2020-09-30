// Copyright 2018-2020 Drexel University
// Author: Geoffrey Mainland <mainland@drexel.edu>

#include "liquid/OFDM.hh"

namespace Liquid {

OFDMSubcarriers::OFDMSubcarriers(unsigned int M)
  : std::vector<char>(M, OFDMFRAME_SCTYPE_NULL)
{
    unsigned int      i;
    unsigned int      M2 = M/2;

    // Compute guard band
    unsigned int G = M / 10;

    if (G < 2)
        G = 2;

    // designate pilot spacing
    unsigned int P = (M > 34) ? 8 : 4;
    unsigned int P2 = P/2;

    // upper band
    for (i=1; i<M2-G; i++) {
        if ( ((i+P2)%P) == 0 )
            (*this)[i] = OFDMFRAME_SCTYPE_PILOT;
        else
            (*this)[i] = OFDMFRAME_SCTYPE_DATA;
    }

    // lower band
    for (i=1; i<M2-G; i++) {
        unsigned int k = M - i;
        if ( ((i+P2)%P) == 0 )
            (*this)[k] = OFDMFRAME_SCTYPE_PILOT;
        else
            (*this)[k] = OFDMFRAME_SCTYPE_DATA;
    }
}

OFDMSubcarriers::OFDMSubcarriers(const std::string &scs)
  : std::vector<char>(scs.size(), OFDMFRAME_SCTYPE_NULL)
{
    *this = scs;
}

OFDMSubcarriers::OFDMSubcarriers(std::initializer_list<char> init)
  : std::vector<char>(init)
{
    validate();
}

OFDMSubcarriers &OFDMSubcarriers::operator =(const std::string &scs)
{
    if (scs.size() != size()) {
        std::stringstream buffer;

        buffer << "OFDMSubcarriers: expected " << size() << " subcarriers but got " << scs.size();

        throw std::range_error(buffer.str());
    }

    for (unsigned i = 0; i < size(); ++i) {
        switch (scs[i]) {
            case '.':
                (*this)[i] = OFDMFRAME_SCTYPE_NULL;
                break;

            case 'P':
                (*this)[i] = OFDMFRAME_SCTYPE_PILOT;
                break;

            case '+':
                (*this)[i] = OFDMFRAME_SCTYPE_DATA;
                break;

            default:
                {
                    std::stringstream buffer;

                    buffer << "OFDMSubcarriers: invalid subcarrier type (" << scs[i] << ")";

                    throw std::range_error(buffer.str());
                }
                break;
        }
    }

    validate();

    return *this;
}

OFDMSubcarriers::operator std::string() const
{
    std::string scs(size(), '.');

    for (unsigned i = 0; i < size(); ++i) {
        switch ((*this)[i])  {
            case OFDMFRAME_SCTYPE_NULL:
                scs[i] = '.';
                break;

            case OFDMFRAME_SCTYPE_PILOT:
                scs[i] = 'P';
                break;

            case OFDMFRAME_SCTYPE_DATA:
                scs[i] = '+';
                break;

            default:
                {
                    std::stringstream buffer;

                    buffer << "OFDMSubcarriers: invalid subcarrier type (" << (*this)[i] << ")";

                    throw std::range_error(buffer.str());
                }
                break;
        }
    }

    return scs;
}

void OFDMSubcarriers::validate(void)
{
    unsigned nnull = 0;
    unsigned npilot = 0;
    unsigned ndata = 0;

    for (unsigned i=0; i < size(); i++) {
        // update appropriate counters
        if ((*this)[i] == OFDMFRAME_SCTYPE_NULL)
            nnull++;
        else if ((*this)[i] == OFDMFRAME_SCTYPE_PILOT)
            npilot++;
        else if ((*this)[i] == OFDMFRAME_SCTYPE_DATA)
            ndata++;
        else {
            std::stringstream buffer;

            buffer << "OFDMSubcarriers: invalid subcarrier type (" << (*this)[i] << ")";

            throw std::range_error(buffer.str());
        }
    }

    if (npilot + ndata == 0)
        throw std::range_error("OFDMSubcarriers: must have at least one enabled subcarrier");
    else if (ndata == 0)
        throw std::range_error("OFDMSubcarriers: must have at least one data subcarriers");
    else if (npilot < 2)
        throw std::range_error("OFDMSubcarriers: must have at least two pilot subcarriers");
}

}
