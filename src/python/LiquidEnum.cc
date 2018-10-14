#include <liquid/liquid.h>

#include "python/PyModules.hh"

void exportLiquidEnums(py::module &m)
{
    // Create enum type CRCScheme for liquid CRC schemes
    py::enum_<crc_scheme> crc(m, "CRCScheme");

    crc.def(py::init([](std::string value) -> crc_scheme {
            auto crc = liquid_getopt_str2crc(value.c_str());
            if (crc == LIQUID_CRC_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type CRCScheme");
            return crc;
        }));

    py::implicitly_convertible<py::str, crc_scheme>();

    for (unsigned int i = 0; i < LIQUID_CRC_NUM_SCHEMES; ++i)
        crc.value(crc_scheme_str[i][0], static_cast<crc_scheme>(i));

    crc.export_values();

    // Create enum type FECScheme for liquid FEC schemes
    py::enum_<fec_scheme> fec(m, "FECScheme");

    fec.def(py::init([](std::string value) -> fec_scheme {
            auto fec = liquid_getopt_str2fec(value.c_str());
            if (fec == LIQUID_FEC_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type FECScheme");
            return fec;
        }));

    py::implicitly_convertible<py::str, fec_scheme>();

    for (unsigned int i = 0; i < LIQUID_FEC_NUM_SCHEMES; ++i)
        fec.value(fec_scheme_str[i][0], static_cast<fec_scheme>(i));

    fec.export_values();

    // Create enum type ModulationScheme for liquid modulation schemes
    py::enum_<modulation_scheme> ms(m, "ModulationScheme");

    ms.def(py::init([](std::string value) -> modulation_scheme {
           auto ms = liquid_getopt_str2mod(value.c_str());
           if (ms == LIQUID_MODEM_UNKNOWN)
               throw py::value_error("\"" + value + "\" is not a valid value for enum type ModulationScheme");
           return ms;
       }));

    py::implicitly_convertible<py::str, modulation_scheme>();

    for (unsigned int i = 0; i < LIQUID_MODEM_NUM_SCHEMES; ++i)
        ms.value(modulation_types[i].name, static_cast<modulation_scheme>(i));

    ms.export_values();
}
