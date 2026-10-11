#include <pybind11/pybind11.h>

#include "quantMicrostructure/core/build_info.hpp"

#include <string>

namespace py = pybind11;

PYBIND11_MODULE(quantmicrostructure, m)
{
    m.doc() = "quantMicrostructure: order book, matching engine, flow simulators, "
              "execution and market-making models";

    const quantMicrostructure::BuildInfo &info = quantMicrostructure::build_info();
    m.attr("__version__") = std::string(info.version);

    m.def(
        "build_info",
        []()
        {
            const quantMicrostructure::BuildInfo &b = quantMicrostructure::build_info();
            py::dict out;
            out["version"] = std::string(b.version);
            out["git_sha"] = std::string(b.git_sha);
            return out;
        },
        "What to record next to a result to reproduce it: the version of the library\n"
        "and the commit it was built from.");
}
