/*
 * Copyright (c) 2017-2022, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sbi/sbi_console.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/psci/psci.h>
#include <sbi_utils/psci/plat/arm/common/plat_arm.h>
#include <sbi_utils/psci/drivers/arm/css/scmi.h>
#include <sbi_utils/psci/plat/arm/css/common/css_pm.h>
#include <sbi_utils/psci/plat/arm/common/arm_def.h>
#include <sbi_utils/psci/plat/arm/board/spacemit/include/platform_def.h>
#include <sbi_utils/psci/plat/common/platform.h>
#include <../../../psci/psci_private.h>

/*
 * This file implements the SCP helper functions using SCMI protocol.
 */

/*
 * SCMI power state parameter bit field encoding for ARM CSS platforms.
 *
 * 31  20 19       16 15      12 11       8 7        4 3         0
 * +-------------------------------------------------------------+
 * | SBZ | Max level |  Level 3 |  Level 2 |  Level 1 |  Level 0 |
 * |     |           |   state  |   state  |   state  |   state  |
 * +-------------------------------------------------------------+
 *
 * `Max level` encodes the highest level that has a valid power state
 * encoded in the power state.
 */
#define SCMI_PWR_STATE_MAX_PWR_LVL_SHIFT	16
#define SCMI_PWR_STATE_MAX_PWR_LVL_WIDTH	4
#define SCMI_PWR_STATE_MAX_PWR_LVL_MASK		\
				((1 << SCMI_PWR_STATE_MAX_PWR_LVL_WIDTH) - 1)
#define SCMI_SET_PWR_STATE_MAX_PWR_LVL(_power_state, _max_level)		\
		(_power_state) |= ((_max_level) & SCMI_PWR_STATE_MAX_PWR_LVL_MASK)\
				<< SCMI_PWR_STATE_MAX_PWR_LVL_SHIFT
#define SCMI_GET_PWR_STATE_MAX_PWR_LVL(_power_state)		\
		(((_power_state) >> SCMI_PWR_STATE_MAX_PWR_LVL_SHIFT)	\
				& SCMI_PWR_STATE_MAX_PWR_LVL_MASK)

#define SCMI_PWR_STATE_LVL_WIDTH		4
#define SCMI_PWR_STATE_LVL_MASK			\
				((1 << SCMI_PWR_STATE_LVL_WIDTH) - 1)
#define SCMI_SET_PWR_STATE_LVL(_power_state, _level, _level_state)		\
		(_power_state) |= ((_level_state) & SCMI_PWR_STATE_LVL_MASK)	\
				<< (SCMI_PWR_STATE_LVL_WIDTH * (_level))
#define SCMI_GET_PWR_STATE_LVL(_power_state, _level)		\
		(((_power_state) >> (SCMI_PWR_STATE_LVL_WIDTH * (_level))) &	\
				SCMI_PWR_STATE_LVL_MASK)

/*
 * The SCMI power state enumeration for a power domain level
 */
typedef enum {
	scmi_power_state_off = 0,
	scmi_power_state_on = 1,
	scmi_power_state_sleep = 2,
} scmi_power_state_t;

/*
 * The global handles for invoking the SCMI driver APIs after the driver
 * has been initialized.
 */
static void *scmi_handles[PLAT_ARM_SCMI_CHANNEL_COUNT];

/* The global SCMI channels array */
static scmi_channel_t scmi_channels[PLAT_ARM_SCMI_CHANNEL_COUNT];

/*
 * Channel ID for the default SCMI channel.
 * The default channel is used to issue SYSTEM level SCMI requests and is
 * initialized to the channel which has the boot cpu as its resource.
 */
static uint32_t default_scmi_channel_id;

/*
 * TODO: Allow use of channel specific lock instead of using a single lock for
 * all the channels.
 */
ARM_SCMI_INSTANTIATE_LOCK;

/*
 * Function to obtain the SCMI Domain ID and SCMI Channel number from the linear
 * core position. The SCMI Channel number is encoded in the upper 16 bits and
 * the Domain ID is encoded in the lower 16 bits in each entry of the mapping
 * array exported by the platform.
 */
static void css_scp_core_pos_to_scmi_channel(unsigned int core_pos,
		unsigned int *scmi_domain_id, unsigned int *scmi_channel_id)
{
	unsigned int composite_id;
	unsigned int *map_id = plat_get_power_domain_tree_desc()[CLUSTER_INDEX_IN_CPU_TOPOLOGY] > 1 ? 
		plat_css_core_pos_to_scmi_dmn_id_map[1] :
		plat_css_core_pos_to_scmi_dmn_id_map[0];

	composite_id = map_id[core_pos];

	*scmi_channel_id = GET_SCMI_CHANNEL_ID(composite_id);
	*scmi_domain_id = GET_SCMI_DOMAIN_ID(composite_id);
}

/*
 * Helper function to turn off a CPU power domain and its parent power domains
 * if applicable.
 */
void css_scp_off(const struct psci_power_state *target_state)
{
	unsigned int lvl = 0, channel_id, domain_id;
	int ret;
	uint32_t scmi_pwr_state = 0, cpu_idx;
	unsigned int hartid = current_hartid();

	cpu_idx = plat_core_pos_by_mpidr(hartid);

	/* At-least the CPU level should be specified to be OFF */
	if (target_state->pwr_domain_state[ARM_PWR_LVL0] != ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d, wrong power domain state\n",
				__func__, __LINE__);
		sbi_hart_hang();
	}

	/* PSCI CPU OFF cannot be used to turn OFF system power domain */
	if (css_system_pwr_state(target_state) != ARM_LOCAL_STATE_RUN) {
		sbi_printf("%s:%d, wrong power domain state\n",
				__func__, __LINE__);
		sbi_hart_hang();
	}

	for (; lvl <= PLAT_MAX_PWR_LVL; lvl++) {
		if (target_state->pwr_domain_state[lvl] == ARM_LOCAL_STATE_RUN)
			break;

		if (target_state->pwr_domain_state[lvl] != ARM_LOCAL_STATE_OFF) {
			sbi_printf("%s:%d, wrong power domain state\n",
					__func__, __LINE__);
			sbi_hart_hang();
		}

		SCMI_SET_PWR_STATE_LVL(scmi_pwr_state, lvl,
				scmi_power_state_off);
	}

	SCMI_SET_PWR_STATE_MAX_PWR_LVL(scmi_pwr_state, lvl - 1);

	css_scp_core_pos_to_scmi_channel(cpu_idx, &domain_id, &channel_id);
	ret = scmi_pwr_state_set(scmi_handles[channel_id],
		domain_id, scmi_pwr_state);
	if (ret != SCMI_E_QUEUED && ret != SCMI_E_SUCCESS) {
		sbi_printf("SCMI set power state command return 0x%x unexpected\n",
				ret);
		sbi_hart_hang();
	}
}

/*
 * Helper function to turn ON a CPU power domain and its parent power domains
 * if applicable.
 */
void css_scp_on(u_register_t mpidr)
{
	unsigned int lvl = 0, channel_id, core_pos, domain_id;
	int ret;
	uint32_t scmi_pwr_state = 0;

	core_pos = plat_core_pos_by_mpidr(mpidr);
	if (core_pos >= PLATFORM_CORE_COUNT) {
		sbi_printf("%s:%d, node_idx beyond the boundary\n",
				__func__, __LINE__);
		sbi_hart_hang();
	}

	for (; lvl <= PLAT_MAX_PWR_LVL; lvl++)
		SCMI_SET_PWR_STATE_LVL(scmi_pwr_state, lvl,
				scmi_power_state_on);

	SCMI_SET_PWR_STATE_MAX_PWR_LVL(scmi_pwr_state, lvl - 1);

	css_scp_core_pos_to_scmi_channel(core_pos, &domain_id,
			&channel_id);
	ret = scmi_pwr_state_set(scmi_handles[channel_id],
		domain_id, scmi_pwr_state);
	if (ret != SCMI_E_QUEUED && ret != SCMI_E_SUCCESS) {
		sbi_printf("SCMI set power state command return 0x%x unexpected\n",
				ret);
		sbi_hart_hang();
	}
}

/*
 * Helper function to get the power state of a power domain node as reported
 * by the SCP.
 */
int css_scp_get_power_state(u_register_t mpidr, unsigned int power_level)
{
	int ret;
	uint32_t scmi_pwr_state = 0, lvl_state;
	unsigned int channel_id, cpu_idx, domain_id;

	cpu_idx = plat_core_pos_by_mpidr(mpidr);

	if (cpu_idx >= PLATFORM_CORE_COUNT) {
		sbi_printf("%s:%d, node_idx beyond the boundary\n",
				__func__, __LINE__);
		sbi_hart_hang();
	}

	/* We don't support get power state at the system power domain level */
	if ((power_level > PLAT_MAX_PWR_LVL) ||
			(power_level == CSS_SYSTEM_PWR_DMN_LVL)) {
		sbi_printf("Invalid power level %u specified for SCMI get power state\n",
				power_level);
		return PSCI_E_INVALID_PARAMS;
	}

	css_scp_core_pos_to_scmi_channel(cpu_idx, &domain_id, &channel_id);
	ret = scmi_pwr_state_get(scmi_handles[channel_id],
		domain_id, &scmi_pwr_state);

	if (ret != SCMI_E_SUCCESS) {
		sbi_printf("SCMI get power state command return 0x%x unexpected\n",
				ret);
		return PSCI_E_INVALID_PARAMS;
	}

	/*
	 * Find the maximum power level described in the get power state
	 * command. If it is less than the requested power level, then assume
	 * the requested power level is ON.
	 */
	if (SCMI_GET_PWR_STATE_MAX_PWR_LVL(scmi_pwr_state) < power_level)
		return HW_ON;

	lvl_state = SCMI_GET_PWR_STATE_LVL(scmi_pwr_state, power_level);
	if (lvl_state == scmi_power_state_on)
		return HW_ON;

	if ((lvl_state != scmi_power_state_off) &&
				(lvl_state != scmi_power_state_sleep)) {
		sbi_printf("wrong power state, :%d\n", ret);
		sbi_hart_hang();

	}

	return HW_OFF;
}

void plat_arm_pwrc_setup(void)
{
	unsigned int composite_id, idx, cpu_idx;
	unsigned int hartid = current_hartid();

	cpu_idx = plat_core_pos_by_mpidr(hartid);

	for (idx = 0; idx < PLAT_ARM_SCMI_CHANNEL_COUNT; idx++) {
		sbi_printf("Initializing SCMI driver on channel %d\n", idx);

		scmi_channels[idx].info = plat_css_get_scmi_info(idx);
		scmi_channels[idx].lock = ARM_SCMI_LOCK_GET_INSTANCE;
		scmi_handles[idx] = scmi_init(&scmi_channels[idx]);

		if (scmi_handles[idx] == NULL) {
			sbi_printf("SCMI Initialization failed on channel %d\n", idx);
			sbi_hart_hang();
		}
	}

	unsigned int *map_id = plat_get_power_domain_tree_desc()[CLUSTER_INDEX_IN_CPU_TOPOLOGY] > 1 ? 
		plat_css_core_pos_to_scmi_dmn_id_map[1] :
		plat_css_core_pos_to_scmi_dmn_id_map[0];

	composite_id = map_id[cpu_idx];
	default_scmi_channel_id = GET_SCMI_CHANNEL_ID(composite_id);
}

/******************************************************************************
 * This function overrides the default definition for ARM platforms. Initialize
 * the SCMI driver, query capability via SCMI and modify the PSCI capability
 * based on that.
 *****************************************************************************/
const plat_psci_ops_t *css_scmi_override_pm_ops(plat_psci_ops_t *ops)
{
	uint32_t msg_attr;
	int ret;
	void *scmi_handle = scmi_handles[default_scmi_channel_id];

	/* Check that power domain POWER_STATE_SET message is supported */
	ret = scmi_proto_msg_attr(scmi_handle, SCMI_PWR_DMN_PROTO_ID,
				SCMI_PWR_STATE_SET_MSG, &msg_attr);
	if (ret != SCMI_E_SUCCESS) {
		sbi_printf("Set power state command is not supported by SCMI\n");
		sbi_hart_hang();
	}

	/*
	 * Don't support PSCI NODE_HW_STATE call if SCMI doesn't support
	 * POWER_STATE_GET message.
	 */
	ret = scmi_proto_msg_attr(scmi_handle, SCMI_PWR_DMN_PROTO_ID,
				SCMI_PWR_STATE_GET_MSG, &msg_attr);
	if (ret != SCMI_E_SUCCESS)
		ops->get_node_hw_state = NULL;

	/* Check if the SCMI SYSTEM_POWER_STATE_SET message is supported */
	ret = scmi_proto_msg_attr(scmi_handle, SCMI_SYS_PWR_PROTO_ID,
				SCMI_SYS_PWR_STATE_SET_MSG, &msg_attr);
	if (ret != SCMI_E_SUCCESS) {
		/* System power management operations are not supported */
		ops->system_off = NULL;
		ops->system_reset = NULL;
		ops->get_sys_suspend_power_state = NULL;
	} else {
		if (!(msg_attr & SCMI_SYS_PWR_SUSPEND_SUPPORTED)) {
			/*
			 * System power management protocol is available, but
			 * it does not support SYSTEM SUSPEND.
			 */
			ops->get_sys_suspend_power_state = NULL;
		}
		if (!(msg_attr & SCMI_SYS_PWR_WARM_RESET_SUPPORTED)) {
			/*
			 * WARM reset is not available.
			 */
			ops->system_reset2 = NULL;
		}
	}

	return ops;
}

/*
 * Helper function to suspend a CPU power domain and its parent power domains
 * if applicable.
 */
void css_scp_suspend(const struct psci_power_state *target_state)
{
        int ret;
	unsigned int curr_hart = current_hartid();

	unsigned int core_pos = plat_core_pos_by_mpidr(curr_hart);
        if (core_pos >= PLATFORM_CORE_COUNT) {
                sbi_printf("%s:%d, node_idx beyond the boundary\n",
                                __func__, __LINE__);
                sbi_hart_hang();
        }


        /* At least power domain level 0 should be specified to be suspended */
        if (target_state->pwr_domain_state[ARM_PWR_LVL0] !=
                                                ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        /* Check if power down at system power domain level is requested */
        if (css_system_pwr_state(target_state) == ARM_LOCAL_STATE_OFF) {
                /* Issue SCMI command for SYSTEM_SUSPEND on all SCMI channels */
                ret = scmi_sys_pwr_state_set(
                                scmi_handles[default_scmi_channel_id],
                                SCMI_SYS_PWR_FORCEFUL_REQ, SCMI_SYS_PWR_SUSPEND);
                if (ret != SCMI_E_SUCCESS) {
			sbi_printf("SCMI system power domain suspend return 0x%x unexpected\n",
                                        ret);
                        sbi_hart_hang();
                }
                return;
        }

        unsigned int lvl, channel_id, domain_id;
        uint32_t scmi_pwr_state = 0;
        /*
         * If we reach here, then assert that power down at system power domain
         * level is running.
         */
        if (css_system_pwr_state(target_state) != ARM_LOCAL_STATE_RUN) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        /* For level 0, specify `scmi_power_state_sleep` as the power state */
        SCMI_SET_PWR_STATE_LVL(scmi_pwr_state, ARM_PWR_LVL0,
                                                scmi_power_state_sleep);

        for (lvl = ARM_PWR_LVL1; lvl <= PLAT_MAX_PWR_LVL; lvl++) {
                if (target_state->pwr_domain_state[lvl] == ARM_LOCAL_STATE_RUN)
                        break;

                if (target_state->pwr_domain_state[lvl] !=
                                                        ARM_LOCAL_STATE_OFF) {
			sbi_printf("%s:%d\n", __func__, __LINE__);
			sbi_hart_hang();
		}
                /*
                 * Specify `scmi_power_state_off` as power state for higher
                 * levels.
                 */
                SCMI_SET_PWR_STATE_LVL(scmi_pwr_state, lvl,
                                                scmi_power_state_off);
        }

        SCMI_SET_PWR_STATE_MAX_PWR_LVL(scmi_pwr_state, lvl - 1);

        css_scp_core_pos_to_scmi_channel(core_pos,
                        &domain_id, &channel_id);
        ret = scmi_pwr_state_set(scmi_handles[channel_id],
                domain_id, scmi_pwr_state);

        if (ret != SCMI_E_SUCCESS) {
                sbi_printf("SCMI set power state command return 0x%x unexpected\n",
                                ret);
                sbi_hart_hang();
        }
}
