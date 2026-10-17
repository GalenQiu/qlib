#ifndef QLIB_QLIB_H
#define QLIB_QLIB_H

/**
 * @file qlib.h
 * @brief Umbrella header: pulls in every public qlib module.
 */

#include "qlib/ql_common.h"
#include "qlib/ql_hash.h"
#include "qlib/ql_list.h"
#include "qlib/ql_mem.h"
#include "qlib/ql_str.h"
#include "qlib/ql_vector.h"

/*
 * The network module is optional: when a build is configured with
 * QLIB_BUILD_NET=OFF its source and its header are both left out, and
 * QLIB_HAS_NET is not defined. Guarding the include keeps this umbrella header
 * usable in either configuration.
 */
#ifdef QLIB_HAS_NET
#  include "qlib/ql_net.h"
#endif

#ifdef QLIB_HAS_THREADS
#  include "qlib/ql_thread.h"
#endif

#ifdef QLIB_HAS_LOG
#  include "qlib/ql_log.h"
#endif

#endif /* QLIB_QLIB_H */
