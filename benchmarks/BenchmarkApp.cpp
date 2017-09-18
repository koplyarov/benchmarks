// Copyright (c) 2016, Dmitry Koplyarov <koplyarov.da@gmail.com>
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


#include <benchmarks/BenchmarkApp.hpp>

#include <benchmarks/detail/ReportTemplateProcessor.hpp>
#include <benchmarks/ipc/MessageQueue.hpp>
#include <benchmarks/utils/ThreadPriority.hpp>

#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <boost/scope_exit.hpp>
#include <boost/spirit/include/support_multi_pass.hpp>

#include <fstream>
#include <iomanip>


namespace benchmarks
{

	using OperationTimesMap = std::map<std::string, double>	;
	using MemoryConsumptionMap = std::map<std::string, int64_t>;


	class BenchmarksResultsReporter : public IBenchmarksResultsReporter
	{
	private:
		static NamedLogger		s_logger;
		OperationTimesMap		_operationTimes;
		MemoryConsumptionMap	_memoryConsumption;

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


	BENCHMARKS_LOGGER(BenchmarkApp);

	BenchmarkApp::BenchmarkApp(const BenchmarkSuite& suite)
		: _suite(suite), _queueName("wigwagMessageQueue"), _verbosity(1), _repeatCount(1)
	{ }


	namespace
	{
		struct CmdLineException : public std::runtime_error
		{ CmdLineException(const std::string& msg) : std::runtime_error(msg) { } };
	}


	int BenchmarkApp::Run(int argc, char* argv[])
	{
		try
		{
			using namespace boost::program_options;

			_executableName = argv[0];

			std::string subtask, benchmark, template_filename, output_filename;
			int64_t num_iterations = 0;
			std::vector<std::string> params_vec;

			options_description od;
			od.add_options()
				("help", "Show help")
				("verbosity,v", value<int64_t>(&_verbosity), "Verbosity in range [0..3], default: 1")
				("count,c", value<int64_t>(&_repeatCount), "Measurements count, default: 1")
				("benchmark,b", value<std::string>(&benchmark), "Benchmark id")
				("params", value<std::vector<std::string>>(&params_vec)->multitoken(), "Benchmark parameters")
				("template,t", value<std::string>(&template_filename), "Template file")
				("output,o", value<std::string>(&output_filename), "Output file")
				("subtask", value<std::string>(&subtask), "Internal option")
				("queue", value<std::string>(&_queueName), "Internal option")
				("iterations", value<int64_t>(&num_iterations), "Internal option")
				;

			positional_options_description pd;
			pd.add("benchmark", 1).add("params", -1);

			parsed_options parsed = command_line_parser(argc, argv).options(od).positional(pd).run();

			variables_map vm;
			store(parsed, vm);
			notify(vm);

			if ((benchmark.empty() && (template_filename.empty() || output_filename.empty())) || vm.count("help"))
			{
				std::cerr << boost::lexical_cast<std::string>(od) << std::endl;
				return 0;
			}

			switch (_verbosity)
			{
			case 0: Logger::SetLogLevel(LogLevel::Error); break;
			case 1: Logger::SetLogLevel(LogLevel::Warning); break;
			case 2: Logger::SetLogLevel(LogLevel::Info); break;
			case 3: Logger::SetLogLevel(LogLevel::Debug); break;
			default: s_logger.Warning() << "Unexpected verbosity value: " << _verbosity; break;
			}

			if (!benchmark.empty())
			{
				boost::regex benchmark_re(R"(([^.]+)\.([^.]+)\.([^.]+))");
				boost::smatch m;
				if (!boost::regex_match(benchmark, m, benchmark_re))
					throw CmdLineException("Could not parse benchmark id: '" + benchmark + "'!");

				boost::regex param_re(R"(([^:]+):(.+))");
				std::map<std::string, SerializedParam> params;
				for (auto&& param_str : params_vec)
				{
					boost::smatch m;
					if (!boost::regex_match(param_str, m, param_re))
						throw CmdLineException("Could not parse parameter: '" + param_str + "'!");
					params[m[1]] = m[2];
				}

				ParameterizedBenchmarkId benchmark_id({m[1], m[2], m[3]}, params);

				if (subtask == "measureIterationsCount")
				{
					auto iterations_count = _suite.MeasureIterationsCount(benchmark_id);
					MessageQueue mq(_queueName, boost::interprocess::open_only);
					mq.SendMessage(std::make_shared<IterationsCountMessage>(iterations_count));
					return 0;
				}
				else if (subtask == "invokeBenchmark")
				{
					if (vm.count("iterations") == 0)
						throw CmdLineException("Number of iterations is not specified!");
					SetMaxThreadPriority();
					auto results_reporter = std::make_shared<BenchmarksResultsReporter>();
					_suite.InvokeBenchmark(num_iterations, benchmark_id, results_reporter);
					auto result = BenchmarkResult(results_reporter->GetOperationTimes(), results_reporter->GetMemoryConsumption());

					MessageQueue mq(_queueName, boost::interprocess::open_only);
					mq.SendMessage(std::make_shared<BenchmarkResultMessage>(result));
					return 0;
				}
				else if (!subtask.empty())
					throw CmdLineException("Unknown subtask!");

				auto r = RunBenchmark(benchmark_id);
				for (auto p : r.GetOperationTimes())
					s_logger.Info() << p.first << ": " << p.second << " ns";
				for (auto p : r.GetMemoryConsumption())
					s_logger.Info() << p.first << ": " << p.second << " bytes";
			}
			else
			{
				using namespace boost::spirit;

				std::map<ParameterizedBenchmarkId, BenchmarkResult> requested_benchmarks;

				{
					std::ifstream input_stream(template_filename, std::ios_base::binary);
					if (!input_stream.is_open())
						throw std::runtime_error("Could not open " + template_filename);

					ReportTemplateProcessor::Process(
							make_default_multi_pass(std::istreambuf_iterator<char>(input_stream)),
							make_default_multi_pass(std::istreambuf_iterator<char>()),
							[&](char c) { },
							[&](const MeasurementId& id, const boost::optional<MeasurementId>& baselineId)
							{
								requested_benchmarks.insert({id.GetBenchmarkId(), {}});
								if (baselineId)
									requested_benchmarks.insert({baselineId->GetBenchmarkId(), {}});
							}
						);
				}

				size_t n = 0, total = requested_benchmarks.size();
				for (auto&& p : requested_benchmarks)
				{
					s_logger.Info() << "Benchmark " << ++n << "/" << total << ": " << p.first;
					p.second = RunBenchmark(p.first);
				}

				std::ifstream input_stream(template_filename, std::ios_base::binary);
				if (!input_stream.is_open())
					throw std::runtime_error("Could not open " + template_filename);

				std::unique_ptr<std::ofstream> output_file;
				std::ostream* output_stream = &std::cout;
				if (output_filename != "-")
				{
					output_file.reset(new std::ofstream (output_filename, std::ios_base::binary));
					if (!output_file->is_open())
						throw std::runtime_error("Could not open " + output_filename);
					output_stream = output_file.get();
				}

				auto get_measurement = [&](const MeasurementId& id) -> double
					{
						auto&& r = requested_benchmarks.at(id.GetBenchmarkId());

						auto it1 = r.GetOperationTimes().find(id.GetMeasurementLocalId());
						if (it1 != r.GetOperationTimes().end())
							return it1->second;

						auto it2 = r.GetMemoryConsumption().find(id.GetMeasurementLocalId());
						if (it2 == r.GetMemoryConsumption().end())
							throw std::runtime_error("Could not find a measurement with id " + id.ToString());
						return it2->second;
					};

				ReportTemplateProcessor::Process(
						make_default_multi_pass(std::istreambuf_iterator<char>(input_stream)),
						make_default_multi_pass(std::istreambuf_iterator<char>()),
						[&](char c) { *output_stream << c; },
						[&](const MeasurementId& id, const boost::optional<MeasurementId>& baselineId)
						{
							auto val = get_measurement(id);
							if (baselineId)
								val -= get_measurement(*baselineId);

							std::ios::fmtflags f(output_stream->flags());
							BOOST_SCOPE_EXIT_ALL(&) { output_stream->flags(f); };
							if (val < 99)
								*output_stream << std::setprecision(2);
							else
								*output_stream << std::fixed << std::setprecision(0);
							*output_stream << val;
						}
					);
			}

			return 0;
		}
		catch (const CmdLineException& ex)
		{
			std::cerr << ex.what() << std::endl;
			return 1;
		}
		catch (const std::exception& ex)
		{
			s_logger.Error() << "Uncaught exception: " << ex.what();
			return 1;
		}
	}


	BenchmarkResult BenchmarkApp::RunBenchmark(const ParameterizedBenchmarkId& id) const
	{
		std::string benchmark = id.GetId().ToString();

		MessageQueue mq(_queueName, boost::interprocess::create_only);
		BOOST_SCOPE_EXIT_ALL(&) { MessageQueue::Remove(_queueName); };

		{
			std::stringstream cmd;
			cmd << _executableName << " --subtask measureIterationsCount --queue " << _queueName << " --verbosity " << _verbosity << " " << benchmark;
			for (auto&& p : id.GetParams())
				cmd << " " << p.first << ":" << p.second;

			InvokeSubprocess(cmd.str());
		}

		auto it_msg = mq.ReceiveMessage<IterationsCountMessage>();
		BenchmarkResult r;
		for (int64_t i = 0; i < _repeatCount; ++i)
		{
			std::stringstream cmd;
			cmd << _executableName << " --subtask invokeBenchmark --queue " << _queueName << " --verbosity " << _verbosity << " --iterations " << it_msg->GetCount() << " " << benchmark;
			for (auto&& p : id.GetParams())
				cmd << " " << p.first << ":" << p.second;

			InvokeSubprocess(cmd.str());
			auto r_msg = mq.ReceiveMessage<BenchmarkResultMessage>();
			r.Update(r_msg->GetResult());
		}

		return r;
	}


	void BenchmarkApp::InvokeSubprocess(const std::string& cmd)
	{
		s_logger.Debug() << "Invoking " << cmd;
		if (system(cmd.c_str()) != 0)
			throw std::runtime_error(cmd + " failed!");
	}

}
