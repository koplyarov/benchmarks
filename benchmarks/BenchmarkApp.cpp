// Copyright (c) 2016, Dmitry Koplyarov <koplyarov.da@gmail.com>
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.






#include <benchmarks/BenchmarkApp.hpp>

#include <benchmarks/utils/Memory.hpp>
#include <benchmarks/utils/ThreadPriority.hpp>

#include <iostream>
#include <stdexcept>


namespace benchmarks
{

    using OperationTimesMap = std::map<std::string, double> ;
    using MemoryConsumptionMap = std::map<std::string, int64_t>;


    class BenchmarksResultsReporter : public IBenchmarksResultsReporter
    {
    private:
        static NamedLogger      s_logger;
        OperationTimesMap       _operationTimes;
        MemoryConsumptionMap    _memoryConsumption;

    public:
        virtual void ReportOperationDuration(const std::string& name, double ns)
        {
            s_logger.Debug() << name << ": " << ns << " ns";
            _operationTimes[name] = ns;
        }

        virtual void ReportMemoryConsumption(const std::string& name, int64_t bytes)
        {
            s_logger.Debug() << name << ": " << bytes << " bytes";
            _memoryConsumption[name] = bytes;
        }

        const OperationTimesMap& GetOperationTimes() const { return _operationTimes; }
        const MemoryConsumptionMap& GetMemoryConsumption() const { return _memoryConsumption; }
    };
    BENCHMARKS_LOGGER(BenchmarksResultsReporter);


    namespace
    {
        struct CmdLineException : public std::runtime_error
        { CmdLineException(const std::string& msg) : std::runtime_error(msg) { } };

        template < typename String_ >
        void SplitStringImpl(const std::string& src, size_t pos, char delim, String_& dst)
        {
            auto delim_pos = src.find(delim, pos);
            if (delim_pos != std::string::npos)
                throw std::runtime_error(std::string("Too many '") + delim + "' symbols in '" + src + "'");
            dst = src.substr(pos);
        }

        template < typename String1_, typename... Strings_ >
        void SplitStringImpl(const std::string& src, size_t pos, char delim, String1_& dst1, Strings_&... dst)
        {
            auto delim_pos = src.find(delim, pos);
            if (delim_pos == std::string::npos)
                throw std::runtime_error(std::string("Too few '") + delim + "' symbols in '" + src + "'");
            dst1 = src.substr(pos, (delim_pos - pos));
            SplitStringImpl(src, delim_pos + 1, delim, dst...);
        }

        template < typename String1_, typename... Strings_ >
        void SplitString(const std::string& src, char delim, String1_& dst1, Strings_&... dst)
        { SplitStringImpl(src, 0, delim, dst1, dst...); }
    }


    int RunBenchmarkApp(const BenchmarkSuite& suite, int argc, const char* argv[])
    {
        benchmarks::NamedLogger logger("RunBenchmarkApp");

        try
        {
            std::string subtask, benchmark;
            int64_t num_iterations = -1;
            int64_t verbosity = 1;
            std::vector<std::string> params_vec;

            for (int i = 1; i < argc; ++i)
            {
                std::string arg(argv[i]);

                if (arg.substr(0, 2) == "--")
                {
                    if (++i >= argc)
                        break;
                    std::string val = argv[i];

                    if (arg == "--subtask")
                        subtask.assign(val);
                    else if (arg == "--verbosity")
                        verbosity = stoll(val);
                    else if (arg == "--iterations")
                        num_iterations = stoll(val);
                }
                else
                {
                    if (benchmark.empty())
                        benchmark = arg;
                    else
                        params_vec.push_back(arg);
                }
            }

            if (subtask.empty())
                throw std::runtime_error("subtask not specified");
            if (benchmark.empty())
                throw std::runtime_error("benchmark not specified");

            switch (verbosity)
            {
            case 0: Logger::SetLogLevel(LogLevel::Error); break;
            case 1: Logger::SetLogLevel(LogLevel::Warning); break;
            case 2: Logger::SetLogLevel(LogLevel::Info); break;
            case 3: Logger::SetLogLevel(LogLevel::Verbose); break;
            case 4: Logger::SetLogLevel(LogLevel::Debug); break;
            default: logger.Warning() << "Unexpected verbosity value: " << verbosity; break;
            }

            if (!benchmark.empty())
            {
                std::string className, benchmarkName, objectName;
                SplitString(benchmark, '.', className, benchmarkName, objectName);

                std::map<std::string, SerializedParam> params;
                for (auto&& param_str : params_vec)
                {
                    std::string name, value;
                    SplitString(param_str, ':', name, value);
                    params[name] = value;
                }

                ParameterizedBenchmarkId benchmark_id({className, benchmarkName, objectName}, params);

                if (subtask == "measureIterationsCount")
                {
                    auto iterations_count = suite.MeasureIterationsCount(benchmark_id);
                    std::cout << "{\"iterations_count\":" << iterations_count << "}" << std::endl;
                    return 0;
                }
                else if (subtask == "invokeBenchmark")
                {
                    if (num_iterations < 0)
                        throw CmdLineException("Number of iterations is not specified!");
                    SetMaxThreadPriority();
                    auto results_reporter = std::make_shared<BenchmarksResultsReporter>();
                    suite.InvokeBenchmark(num_iterations, benchmark_id, results_reporter);
                    auto r = BenchmarkResult(results_reporter->GetOperationTimes(), results_reporter->GetMemoryConsumption());

                    const auto& times = r.GetOperationTimes();
                    const auto& memory = r.GetMemoryConsumption();

                    std::cout << "{" << std::endl;
                    std::cout << "  \"times\": {" << std::endl;
                    for (auto it = times.begin(); it != times.end(); ++it)
                        std::cout << "    \"" << it->first << "\": " << it->second << (std::next(it) == times.end() ? "" : ",") << std::endl;
                    std::cout << "  }," << std::endl;
                    std::cout << "  \"memory\": {" << std::endl;
                    for (auto it = memory.begin(); it != memory.end(); ++it)
                        std::cout << "    \"" << it->first << "\": " << it->second << (std::next(it) == memory.end() ? "" : ",") << std::endl;
                    std::cout << "  }" << std::endl;
                    std::cout << "}" << std::endl;
                    return 0;
                }
                else
                    throw CmdLineException("Unknown subtask!");
            }
            else
                throw CmdLineException("Benchmark not specified!");

            return 0;
        }
        catch (const CmdLineException& ex)
        {
            std::cerr << ex.what() << std::endl;
            return 1;
        }
        catch (const std::exception& ex)
        {
            logger.Error() << "Uncaught exception: " << ex.what();
            return 1;
        }
    }

}
