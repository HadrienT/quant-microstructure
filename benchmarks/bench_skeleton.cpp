// Placeholder that keeps the benchmark chain building until lot M2 brings the
// order book and the matching engine. Its timing measures nothing of interest
// and is not reported anywhere.
#include <benchmark/benchmark.h>

#include "quantMicrostructure/core/build_info.hpp"

namespace
{

    void buildInfo(benchmark::State &state)
    {
        for (auto _ : state)
        {
            benchmark::DoNotOptimize(quantMicrostructure::build_info());
        }
    }
    BENCHMARK(buildInfo);

} // namespace
