/*
 * Copyright (c) 2018-2019, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <spacemit/spacemit_config.h>
#include <sbi_utils/psci/drivers/arm/css/scmi.h>
#include <sbi_utils/psci/psci.h>
#include <sbi_utils/psci/plat/arm/common/plat_arm.h>
#include <sbi_utils/psci/drivers/arm/css/scmi.h>
#include <sbi_utils/psci/drivers/arm/css/css_mhu_doorbell.h>

const plat_psci_ops_t *plat_arm_psci_override_pm_ops(plat_psci_ops_t *ops)
{
	return css_scmi_override_pm_ops(ops);
}

static scmi_channel_plat_info_t spacemit_scmi_plat_info = {
	.scmi_mbx_mem = SCMI_MAILBOX_SHARE_MEM,
	.db_reg_addr = PLAT_MAILBOX_REG_BASE,
	/* no used */
	.db_preserve_mask = 0xfffffffe,
	/* no used */
	.db_modify_mask = 0x1,
	.ring_doorbell = &mhu_ring_doorbell,
};

scmi_channel_plat_info_t *plat_css_get_scmi_info(unsigned int channel_id)
{
        return &spacemit_scmi_plat_info;
}

/*
 * The array mapping platform core position (implemented by plat_my_core_pos())
 * to the SCMI power domain ID implemented by SCP.
 */
uint32_t plat_css_core_pos_to_scmi_dmn_id_map[PLATFORM_CLUSTER_COUNT][PLATFORM_CORE_COUNT] = {
	PLAT_SCMI_SINGLE_CLUSTER_DOMAIN_MAP,
	PLAT_SCMI_DOUBLE_CLUSTER_DOMAIN_MAP
};
