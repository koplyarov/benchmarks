#ifndef BENCHMARKS_DETAIL_CONFIG_HPP
#define BENCHMARKS_DETAIL_CONFIG_HPP


#if !defined(BENCHMARKS_CFG_DEBUG)
#   if defined(NDEBUG)
#       define BENCHMARKS_CFG_DEBUG 0
#   else
#       define BENCHMARKS_CFG_DEBUG 1
#   endif
#endif


#endif
