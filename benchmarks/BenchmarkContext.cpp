#include <benchmarks/BenchmarkContext.hpp>


namespace benchmarks
{

    void BenchmarkContext::DoWarmUp(const std::function<void()>& func, size_t numWarmUpPasses) const
    {
        for (size_t i = 0; i < numWarmUpPasses; ++i)
            func();
    }

}
