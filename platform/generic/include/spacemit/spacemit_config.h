#ifndef __SPACEMIT_CONFIG_H__
#define __SPACEMIT_CONFIG_H__

#if defined(CONFIG_PLATFORM_SPACEMIT_K1PRO)
#include "./k1pro/core_common.h"

#if defined(CONFIG_PLATFORM_SPACEMIT_K1PRO_FPGA)
#include "./k1pro/k1pro_fpga.h"
#elif defined(CONFIG_PLATFORM_SPACEMIT_K1PRO_QEMU)
#include "./k1pro/k1pro_qemu.h"
#elif defined(CONFIG_PLATFORM_SPACEMIT_K1PRO_SIM)
#include "./k1pro/k1pro_sim.h"
#elif defined(CONFIG_PLATFORM_SPACEMIT_K1PRO_VERIFY)
#include "./k1pro/k1pro_verify.h"
#endif

#endif

#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
#include "./k1x/core_common.h"

#if defined(CONFIG_PLATFORM_SPACEMIT_K1X_FPGA)
#include "./k1x/k1x_fpga.h"
#elif defined(CONFIG_PLATFORM_SPACEMIT_K1X_EVB)
#include "./k1x/k1x_evb.h"
#endif

#endif

#endif /* __SPACEMIT_CONFIG_H__ */
