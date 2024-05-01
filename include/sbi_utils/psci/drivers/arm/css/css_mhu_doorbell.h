/*
 * Copyright (c) 2014-2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CSS_MHU_DOORBELL_H
#define CSS_MHU_DOORBELL_H

#include <sbi_utils/psci/drivers/arm/css/scmi.h>

void mhu_ring_doorbell(struct scmi_channel_plat_info *plat_info);

#endif	/* CSS_MHU_DOORBELL_H */
