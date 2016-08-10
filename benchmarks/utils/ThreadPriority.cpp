// Copyright (c) 2016, Dmitry Koplyarov <koplyarov.da@gmail.com>
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
// provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
// WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


#include <benchmarks/utils/Logger.hpp>
#include <benchmarks/utils/ThreadPriority.hpp>

#include <iostream>


#if defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#	include <pthread.h>
#	include <string.h>
#endif
#if _WIN32
#	include <windows.h>
#endif

namespace benchmarks
{

	static NamedLogger g_logger("ThreadPriority");

	void SetMaxThreadPriority()
	{
#if defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
		int policy = SCHED_FIFO;
		struct sched_param scheduler_params = {};
		scheduler_params.sched_priority = sched_get_priority_max(policy);
		int res = pthread_setschedparam(pthread_self(), policy, &scheduler_params);
		if (res != 0)
			g_logger.Info() << "Could not set thread priority: " << strerror(res);
#endif
#if _WIN32
		if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
		{
			DWORD err = GetLastError();
			char buf[256] = { '\0' };
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf) - 1, NULL);
			g_logger.Info() << "Could not set thread priority: " << buf;
		}
#endif
	}

}