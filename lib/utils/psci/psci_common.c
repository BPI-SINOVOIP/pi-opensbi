/*
 * Copyright (c) 2013-2022, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sbi_utils/psci/psci.h>
#include <sbi_utils/cache/cacheflush.h>
#include <sbi_utils/psci/plat/common/platform.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include <spacemit/spacemit_config.h>
#include "psci_private.h"

/*
 * PSCI requested local power state map. This array is used to store the local
 * power states requested by a CPU for power levels from level 1 to
 * PLAT_MAX_PWR_LVL. It does not store the requested local power state for power
 * level 0 (PSCI_CPU_PWR_LVL) as the requested and the target power state for a
 * CPU are the same.
 *
 * During state coordination, the platform is passed an array containing the
 * local states requested for a particular non cpu power domain by each cpu
 * within the domain.
 *
 * TODO: Dense packing of the requested states will cause cache thrashing
 * when multiple power domains write to it. If we allocate the requested
 * states at each power level in a cache-line aligned per-domain memory,
 * the cache thrashing can be avoided.
 */
static plat_local_state_t
	/* psci_req_local_pwr_states[PLAT_MAX_PWR_LVL][PLATFORM_CORE_COUNT] */
	psci_req_local_pwr_states[PLAT_MAX_PWR_LVL][CACHE_LINE_SIZE] __attribute__((aligned(CACHE_LINE_SIZE)));

unsigned int psci_plat_core_count;

unsigned long psci_delta_off;

/*******************************************************************************
 * Arrays that hold the platform's power domain tree information for state
 * management of power domains.
 * Each node in the array 'psci_non_cpu_pd_nodes' corresponds to a power domain
 * which is an ancestor of a CPU power domain.
 * Each node in the array 'psci_cpu_pd_nodes' corresponds to a cpu power domain
 ******************************************************************************/
non_cpu_pd_node_t psci_non_cpu_pd_nodes[PSCI_NUM_NON_CPU_PWR_DOMAINS];

/* Lock for PSCI state coordination */
DEFINE_PSCI_LOCK(psci_locks[PSCI_NUM_NON_CPU_PWR_DOMAINS]);

cpu_pd_node_t psci_cpu_pd_nodes[PLATFORM_CORE_COUNT];

/*******************************************************************************
 * Pointer to functions exported by the platform to complete power mgmt. ops
 ******************************************************************************/
const plat_psci_ops_t *psci_plat_pm_ops;

/*
 * The plat_local_state used by the platform is one of these types: RUN,
 * RETENTION and OFF. The platform can define further sub-states for each type
 * apart from RUN. This categorization is done to verify the sanity of the
 * psci_power_state passed by the platform and to print debug information. The
 * categorization is done on the basis of the following conditions:
 *
 * 1. If (plat_local_state == 0) then the category is STATE_TYPE_RUN.
 *
 * 2. If (0 < plat_local_state <= PLAT_MAX_RET_STATE), then the category is
 *    STATE_TYPE_RETN.
 *
 * 3. If (plat_local_state > PLAT_MAX_RET_STATE), then the category is
 *    STATE_TYPE_OFF.
 */
typedef enum plat_local_state_type {
        STATE_TYPE_RUN = 0,
        STATE_TYPE_RETN,
        STATE_TYPE_OFF
} plat_local_state_type_t;

/* Function used to categorize plat_local_state. */
plat_local_state_type_t find_local_state_type(plat_local_state_t state)
{
        if (state != 0U) {
                if (state > PLAT_MAX_RET_STATE) {
                        return STATE_TYPE_OFF;
                } else {
                        return STATE_TYPE_RETN;
                }
        } else {
                return STATE_TYPE_RUN;
        }
}

/*******************************************************************************
 * PSCI helper function to get the parent nodes corresponding to a cpu_index.
 ******************************************************************************/
void psci_get_parent_pwr_domain_nodes(unsigned int cpu_idx,
				      unsigned int end_lvl,
				      unsigned int *node_index)
{
	unsigned int parent_node = psci_cpu_pd_nodes[cpu_idx].parent_node;
	unsigned int i;
	unsigned int *node = node_index;

	for (i = PSCI_CPU_PWR_LVL + 1U; i <= end_lvl; i++) {
		*node = parent_node;
		node++;
		parent_node = psci_non_cpu_pd_nodes[parent_node].parent_node;
	}
}

/******************************************************************************
 * This function initializes the psci_req_local_pwr_states.
 *****************************************************************************/
void psci_init_req_local_pwr_states(void)
{
	/* Initialize the requested state of all non CPU power domains as OFF */
	unsigned int pwrlvl;
	unsigned int core;

	for (pwrlvl = 0U; pwrlvl < PLAT_MAX_PWR_LVL; pwrlvl++) {
		for (core = 0; core < psci_plat_core_count; core++) {
			psci_req_local_pwr_states[pwrlvl][core] =
				PLAT_MAX_OFF_STATE;
		}
		csi_dcache_clean_invalid_range(
                        (uintptr_t) psci_req_local_pwr_states[pwrlvl],
                        CACHE_LINE_SIZE);
	}
}

void set_non_cpu_pd_node_local_state(unsigned int parent_idx,
		plat_local_state_t state)
{
	psci_non_cpu_pd_nodes[parent_idx].local_state = state;
	csi_dcache_clean_invalid_range(
                        (uintptr_t) &psci_non_cpu_pd_nodes[parent_idx],
                        sizeof(psci_non_cpu_pd_nodes[parent_idx]));
}

/******************************************************************************
 * Helper function to update the requested local power state array. This array
 * does not store the requested state for the CPU power level. Hence an
 * assertion is added to prevent us from accessing the CPU power level.
 *****************************************************************************/
void psci_set_req_local_pwr_state(unsigned int pwrlvl,
					 unsigned int cpu_idx,
					 plat_local_state_t req_pwr_state)
{
	if ((pwrlvl > PSCI_CPU_PWR_LVL) && (pwrlvl <= PLAT_MAX_PWR_LVL) &&
			(cpu_idx < psci_plat_core_count)) {
		psci_req_local_pwr_states[pwrlvl - 1U][cpu_idx] = req_pwr_state;
		csi_dcache_clean_invalid_range(
                        (uintptr_t) psci_req_local_pwr_states[pwrlvl - 1U],
                        CACHE_LINE_SIZE);
	}
}

/******************************************************************************
 * Helper function to set the target local power state that each power domain
 * from the current cpu power domain to its ancestor at the 'end_pwrlvl' will
 * enter. This function will be called after coordination of requested power
 * states has been done for each power level.
 *****************************************************************************/
static void psci_set_target_local_pwr_states(unsigned int end_pwrlvl,
                                        const psci_power_state_t *target_state)
{
        unsigned int parent_idx, lvl;
	psci_cpu_data_t *svc_cpu_data;
        const plat_local_state_t *pd_state = target_state->pwr_domain_state;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

        psci_set_cpu_local_state(pd_state[PSCI_CPU_PWR_LVL]);

        /*
         * Need to flush as local_state might be accessed with Data Cache
         * disabled during power on
         */
	csi_dcache_clean_invalid_range((uintptr_t)&svc_cpu_data->local_state, sizeof(plat_local_state_t));

        parent_idx = psci_cpu_pd_nodes[plat_core_pos_by_mpidr(hartid)].parent_node;

        /* Copy the local_state from state_info */
        for (lvl = 1U; lvl <= end_pwrlvl; lvl++) {
                set_non_cpu_pd_node_local_state(parent_idx, pd_state[lvl]);
                parent_idx = psci_non_cpu_pd_nodes[parent_idx].parent_node;
        }
}

/******************************************************************************
 * Helper function to return a reference to an array containing the local power
 * states requested by each cpu for a power domain at 'pwrlvl'. The size of the
 * array will be the number of cpu power domains of which this power domain is
 * an ancestor. These requested states will be used to determine a suitable
 * target state for this power domain during psci state coordination. An
 * assertion is added to prevent us from accessing the CPU power level.
 *****************************************************************************/
static plat_local_state_t *psci_get_req_local_pwr_states(unsigned int pwrlvl,
                                                         unsigned int cpu_idx)
{
        if (pwrlvl <= PSCI_CPU_PWR_LVL) {
		sbi_printf("%s:%d, err\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        if ((pwrlvl > PSCI_CPU_PWR_LVL) && (pwrlvl <= PLAT_MAX_PWR_LVL) &&
                        (cpu_idx < psci_plat_core_count)) {
                return &psci_req_local_pwr_states[pwrlvl - 1U][cpu_idx];
        } else
                return NULL;
}

/*
 * Helper functions to get/set the fields of PSCI per-cpu data.
 */
void psci_set_aff_info_state(aff_info_state_t aff_state)
{
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	svc_cpu_data->aff_info_state = aff_state;
}

aff_info_state_t psci_get_aff_info_state(void)
{
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	return svc_cpu_data->aff_info_state;
}

aff_info_state_t psci_get_aff_info_state_by_idx(unsigned int idx)
{
	psci_cpu_data_t *svc_cpu_data;
	const struct sbi_platform *sbi = sbi_platform_thishart_ptr();
	unsigned int hartid = sbi->hart_index2id[idx];
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	return svc_cpu_data->aff_info_state;
}

void psci_set_aff_info_state_by_idx(unsigned int idx,
                                                  aff_info_state_t aff_state)
{
	psci_cpu_data_t *svc_cpu_data;
	const struct sbi_platform *sbi = sbi_platform_thishart_ptr();
	unsigned int hartid = sbi->hart_index2id[idx];
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	svc_cpu_data->aff_info_state = aff_state;
}

void psci_set_cpu_local_state(plat_local_state_t state)
{
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	svc_cpu_data->local_state = state;
}

void psci_set_suspend_pwrlvl(unsigned int target_lvl)
{
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	svc_cpu_data->target_pwrlvl = target_lvl;
}

static inline plat_local_state_t psci_get_cpu_local_state_by_idx(
                unsigned int idx)
{
	psci_cpu_data_t *svc_cpu_data;
	const struct sbi_platform *sbi = sbi_platform_thishart_ptr();
	unsigned int hartid = sbi->hart_index2id[idx];
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	return svc_cpu_data->local_state;
}

static inline plat_local_state_t psci_get_cpu_local_state(void)
{
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	return svc_cpu_data->local_state;
}

/******************************************************************************
 * This function is invoked post CPU power up and initialization. It sets the
 * affinity info state, target power state and requested power state for the
 * current CPU and all its ancestor power domains to RUN.
 *****************************************************************************/
void psci_set_pwr_domains_to_run(unsigned int end_pwrlvl)
{
	unsigned int parent_idx, lvl;
	unsigned int cpu_idx;
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);
	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	cpu_idx = plat_core_pos_by_mpidr(hartid);

	parent_idx = psci_cpu_pd_nodes[cpu_idx].parent_node;

	/* Reset the local_state to RUN for the non cpu power domains. */
	for (lvl = PSCI_CPU_PWR_LVL + 1U; lvl <= end_pwrlvl; lvl++) {
		set_non_cpu_pd_node_local_state(parent_idx,
				PSCI_LOCAL_STATE_RUN);
		psci_set_req_local_pwr_state(lvl,
					     cpu_idx,
					     PSCI_LOCAL_STATE_RUN);
		parent_idx = psci_non_cpu_pd_nodes[parent_idx].parent_node;
	}

	/* Set the affinity info state to ON */
	psci_set_aff_info_state(AFF_STATE_ON);

	psci_set_cpu_local_state(PSCI_LOCAL_STATE_RUN);

	csi_dcache_clean_invalid_range((uintptr_t)svc_cpu_data, sizeof(psci_cpu_data_t));
}

/*******************************************************************************
 * This function prints the state of all power domains present in the
 * system
 ******************************************************************************/
void psci_print_power_domain_map(void)
{
        unsigned int idx;
        plat_local_state_t state;
        plat_local_state_type_t state_type;

        /* This array maps to the PSCI_STATE_X definitions in psci.h */
        static const char * const psci_state_type_str[] = {
                "ON",
                "RETENTION",
                "OFF",
        };

        sbi_printf("PSCI Power Domain Map:\n");
        for (idx = 0; idx < (PSCI_NUM_PWR_DOMAINS - psci_plat_core_count);
                                                        idx++) {
                state_type = find_local_state_type(
                                psci_non_cpu_pd_nodes[idx].local_state);
                sbi_printf("  Domain Node : Level %u, parent_node %u,"
                                " State %s (0x%x)\n",
                                psci_non_cpu_pd_nodes[idx].level,
                                psci_non_cpu_pd_nodes[idx].parent_node,
                                psci_state_type_str[state_type],
                                psci_non_cpu_pd_nodes[idx].local_state);
        }

        for (idx = 0; idx < psci_plat_core_count; idx++) {
                state = psci_get_cpu_local_state_by_idx(idx);
                state_type = find_local_state_type(state);
                sbi_printf("  CPU Node : MPID 0x%llx, parent_node %u,"
                                " State %s (0x%x)\n",
                                (unsigned long long)psci_cpu_pd_nodes[idx].mpidr,
                                psci_cpu_pd_nodes[idx].parent_node,
                                psci_state_type_str[state_type],
                                psci_get_cpu_local_state_by_idx(idx));
        }
}

/*******************************************************************************
 * Simple routine to determine whether a mpidr is valid or not.
 ******************************************************************************/
int psci_validate_mpidr(u_register_t mpidr)
{
        if (plat_core_pos_by_mpidr(mpidr) < 0)
                return PSCI_E_INVALID_PARAMS;

        return PSCI_E_SUCCESS;
}

static unsigned int psci_get_suspend_pwrlvl(void)
{
	psci_cpu_data_t *svc_cpu_data;
	unsigned int hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	return svc_cpu_data->target_pwrlvl;
}

/*******************************************************************************
 * Routine to return the maximum power level to traverse to after a cpu has
 * been physically powered up. It is expected to be called immediately after
 * reset from assembler code.
 ******************************************************************************/
static unsigned int get_power_on_target_pwrlvl(void)
{
        unsigned int pwrlvl;

        /*
         * Assume that this cpu was suspended and retrieve its target power
         * level. If it is invalid then it could only have been turned off
         * earlier. PLAT_MAX_PWR_LVL will be the highest power level a
         * cpu can be turned off to.
         */
        pwrlvl = psci_get_suspend_pwrlvl();
        if (pwrlvl == PSCI_INVALID_PWR_LVL)
                pwrlvl = PLAT_MAX_PWR_LVL;
        if (pwrlvl >= PSCI_INVALID_PWR_LVL) {
		sbi_printf("%s:%d,\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        return pwrlvl;
}

/*******************************************************************************
 * This function is passed the highest level in the topology tree that the
 * operation should be applied to and a list of node indexes. It picks up locks
 * from the node index list in order of increasing power domain level in the
 * range specified.
 ******************************************************************************/
void psci_acquire_pwr_domain_locks(unsigned int end_pwrlvl,
                                   const unsigned int *parent_nodes)
{
        unsigned int parent_idx;
        unsigned int level;

        /* No locking required for level 0. Hence start locking from level 1 */
        for (level = PSCI_CPU_PWR_LVL + 1U; level <= end_pwrlvl; level++) {
                parent_idx = parent_nodes[level - 1U];
                psci_lock_get(&psci_non_cpu_pd_nodes[parent_idx]);
        }
}

/*******************************************************************************
 * This function is passed the highest level in the topology tree that the
 * operation should be applied to and a list of node indexes. It releases the
 * locks in order of decreasing power domain level in the range specified.
 ******************************************************************************/
void psci_release_pwr_domain_locks(unsigned int end_pwrlvl,
                                   const unsigned int *parent_nodes)
{
        unsigned int parent_idx;
        unsigned int level;

        /* Unlock top down. No unlocking required for level 0. */
        for (level = end_pwrlvl; level >= (PSCI_CPU_PWR_LVL + 1U); level--) {
                parent_idx = parent_nodes[level - 1U];
                psci_lock_release(&psci_non_cpu_pd_nodes[parent_idx]);
        }
}

/******************************************************************************
 * This function finds the highest power level which will be powered down
 * amongst all the power levels specified in the 'state_info' structure
 *****************************************************************************/
unsigned int psci_find_max_off_lvl(const psci_power_state_t *state_info)
{
        int i;

        for (i = (int) PLAT_MAX_PWR_LVL; i >= (int) PSCI_CPU_PWR_LVL; i--) {
                if (is_local_state_off(state_info->pwr_domain_state[i]) != 0)
                        return (unsigned int) i;
        }

        return PSCI_INVALID_PWR_LVL;
}

/*
 * The PSCI generic code uses this API to let the platform participate in state
 * coordination during a power management operation. It compares the platform
 * specific local power states requested by each cpu for a given power domain
 * and returns the coordinated target power state that the domain should
 * enter. A platform assigns a number to a local power state. This default
 * implementation assumes that the platform assigns these numbers in order of
 * increasing depth of the power state i.e. for two power states X & Y, if X < Y
 * then X represents a shallower power state than Y. As a result, the
 * coordinated target local power state for a power domain will be the minimum
 * of the requested local power states.
 */
plat_local_state_t plat_get_target_pwr_state(unsigned int lvl,
                                             const plat_local_state_t *states,
                                             unsigned int ncpu)
{
        plat_local_state_t target = PLAT_MAX_OFF_STATE, temp;
        const plat_local_state_t *st = states;
        unsigned int n = ncpu;

        if (ncpu <= 0U) {
		sbi_printf("%s:%d, err\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        do {
                temp = *st;
                st++;
                if (temp < target)
                        target = temp;
                n--;
        } while (n > 0U);

        return target;
}

/*
 * psci_non_cpu_pd_nodes can be placed either in normal memory or coherent
 * memory.
 *
 * With !USE_COHERENT_MEM, psci_non_cpu_pd_nodes is placed in normal memory,
 * it's accessed by both cached and non-cached participants. To serve the common
 * minimum, perform a cache flush before read and after write so that non-cached
 * participants operate on latest data in main memory.
 *
 * When USE_COHERENT_MEM is used, psci_non_cpu_pd_nodes is placed in coherent
 * memory. With HW_ASSISTED_COHERENCY, all PSCI participants are cache-coherent.
 * In both cases, no cache operations are required.
 */

/*
 * Retrieve local state of non-CPU power domain node from a non-cached CPU,
 * after any required cache maintenance operation.
 */
static plat_local_state_t get_non_cpu_pd_node_local_state(
                unsigned int parent_idx)
{
        return psci_non_cpu_pd_nodes[parent_idx].local_state;
}

/******************************************************************************
 * Helper function to return the current local power state of each power domain
 * from the current cpu power domain to its ancestor at the 'end_pwrlvl'. This
 * function will be called after a cpu is powered on to find the local state
 * each power domain has emerged from.
 *****************************************************************************/
void psci_get_target_local_pwr_states(unsigned int end_pwrlvl,
                                      psci_power_state_t *target_state)
{
        unsigned int parent_idx, lvl, cpu_idx;
        plat_local_state_t *pd_state = target_state->pwr_domain_state;
	unsigned int hartid = current_hartid();

	cpu_idx = plat_core_pos_by_mpidr(hartid);
	pd_state[PSCI_CPU_PWR_LVL] = psci_get_cpu_local_state();
        parent_idx = psci_cpu_pd_nodes[cpu_idx].parent_node;

        /* Copy the local power state from node to state_info */
        for (lvl = PSCI_CPU_PWR_LVL + 1U; lvl <= end_pwrlvl; lvl++) {
                pd_state[lvl] = get_non_cpu_pd_node_local_state(parent_idx);
                parent_idx = psci_non_cpu_pd_nodes[parent_idx].parent_node;
        }

        /* Set the the higher levels to RUN */
        for (; lvl <= PLAT_MAX_PWR_LVL; lvl++)
                target_state->pwr_domain_state[lvl] = PSCI_LOCAL_STATE_RUN;
}

/*******************************************************************************
 * Generic handler which is called when a cpu is physically powered on. It
 * traverses the node information and finds the highest power level powered
 * off and performs generic, architectural, platform setup and state management
 * to power on that power level and power levels below it.
 * e.g. For a cpu that's been powered on, it will call the platform specific
 * code to enable the gic cpu interface and for a cluster it will enable
 * coherency at the interconnect level in addition to gic cpu interface.
 ******************************************************************************/
void psci_warmboot_entrypoint(void)
{
        unsigned int end_pwrlvl;
        unsigned int cpu_idx;
        unsigned int parent_nodes[PLAT_MAX_PWR_LVL] = {0};
        psci_power_state_t state_info = { {PSCI_LOCAL_STATE_RUN} };
	unsigned int hartid = current_hartid();

	cpu_idx = plat_core_pos_by_mpidr(hartid);

	/* if we resumed directly from CPU-non-ret because of the wakeup source in suspending process */
	if (psci_get_cpu_local_state() == PSCI_LOCAL_STATE_RUN) {
		/* sbi_printf("%s:%d\n", __func__, __LINE__); */
		return;
	}

        /*
         * Verify that we have been explicitly turned ON or resumed from
         * suspend.
         */
        if (psci_get_aff_info_state() == AFF_STATE_OFF) {
                sbi_printf("Unexpected affinity info state.\n");
                sbi_hart_hang();
        }

        /*
         * Get the maximum power domain level to traverse to after this cpu
         * has been physically powered up.
         */
        end_pwrlvl = get_power_on_target_pwrlvl();

        /* Get the parent nodes */
        psci_get_parent_pwr_domain_nodes(cpu_idx, end_pwrlvl, parent_nodes);

        /*
         * This function acquires the lock corresponding to each power level so
         * that by the time all locks are taken, the system topology is snapshot
         * and state management can be done safely.
         */
        psci_acquire_pwr_domain_locks(end_pwrlvl, parent_nodes);

        psci_get_target_local_pwr_states(end_pwrlvl, &state_info);

#if ENABLE_PSCI_STAT
        plat_psci_stat_accounting_stop(&state_info);
#endif

        /*
         * This CPU could be resuming from suspend or it could have just been
         * turned on. To distinguish between these 2 cases, we examine the
         * affinity state of the CPU:
         *  - If the affinity state is ON_PENDING then it has just been
         *    turned on.
         *  - Else it is resuming from suspend.
         *
         * Depending on the type of warm reset identified, choose the right set
         * of power management handler and perform the generic, architecture
         * and platform specific handling.
         */
        if (psci_get_aff_info_state() == AFF_STATE_ON_PENDING)
                psci_cpu_on_finish(cpu_idx, &state_info);
        else
                psci_cpu_suspend_finish(cpu_idx, &state_info);

        /*
         * Set the requested and target state of this CPU and all the higher
         * power domains which are ancestors of this CPU to run.
         */
        psci_set_pwr_domains_to_run(end_pwrlvl);

#if ENABLE_PSCI_STAT
        /*
         * Update PSCI stats.
         * Caches are off when writing stats data on the power down path.
         * Since caches are now enabled, it's necessary to do cache
         * maintenance before reading that same data.
         */
        psci_stats_update_pwr_up(end_pwrlvl, &state_info);
#endif

        /*
         * This loop releases the lock corresponding to each power level
         * in the reverse order to which they were acquired.
         */
        psci_release_pwr_domain_locks(end_pwrlvl, parent_nodes);
}

/******************************************************************************
 * This function is used in platform-coordinated mode.
 *
 * This function is passed the local power states requested for each power
 * domain (state_info) between the current CPU domain and its ancestors until
 * the target power level (end_pwrlvl). It updates the array of requested power
 * states with this information.
 *
 * Then, for each level (apart from the CPU level) until the 'end_pwrlvl', it
 * retrieves the states requested by all the cpus of which the power domain at
 * that level is an ancestor. It passes this information to the platform to
 * coordinate and return the target power state. If the target state for a level
 * is RUN then subsequent levels are not considered. At the CPU level, state
 * coordination is not required. Hence, the requested and the target states are
 * the same.
 *
 * The 'state_info' is updated with the target state for each level between the
 * CPU and the 'end_pwrlvl' and returned to the caller.
 *
 * This function will only be invoked with data cache enabled and while
 * powering down a core.
 *****************************************************************************/
void psci_do_state_coordination(unsigned int end_pwrlvl,
                                psci_power_state_t *state_info)
{
        unsigned int lvl, parent_idx;
        unsigned int start_idx;
        unsigned int ncpus;
        plat_local_state_t target_state, *req_states;
	unsigned int hartid = current_hartid();
        unsigned int cpu_idx = plat_core_pos_by_mpidr(hartid);;

        if (end_pwrlvl > PLAT_MAX_PWR_LVL) {
		sbi_printf("%s:%d, err\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        parent_idx = psci_cpu_pd_nodes[cpu_idx].parent_node;

        /* For level 0, the requested state will be equivalent
           to target state */
        for (lvl = PSCI_CPU_PWR_LVL + 1U; lvl <= end_pwrlvl; lvl++) {

                /* First update the requested power state */
                psci_set_req_local_pwr_state(lvl, cpu_idx,
                                             state_info->pwr_domain_state[lvl]);

                /* Get the requested power states for this power level */
                start_idx = psci_non_cpu_pd_nodes[parent_idx].cpu_start_idx;
                req_states = psci_get_req_local_pwr_states(lvl, start_idx);

                /*
                 * Let the platform coordinate amongst the requested states at
                 * this power level and return the target local power state.
                 */
                ncpus = psci_non_cpu_pd_nodes[parent_idx].ncpus;
                target_state = plat_get_target_pwr_state(lvl,
                                                         req_states,
                                                         ncpus);

                state_info->pwr_domain_state[lvl] = target_state;

                /* Break early if the negotiated target power state is RUN */
                if (is_local_state_run(state_info->pwr_domain_state[lvl]) != 0)
                        break;

                parent_idx = psci_non_cpu_pd_nodes[parent_idx].parent_node;
        }

        /*
         * This is for cases when we break out of the above loop early because
         * the target power state is RUN at a power level < end_pwlvl.
         * We update the requested power state from state_info and then
         * set the target state as RUN.
         */
        for (lvl = lvl + 1U; lvl <= end_pwrlvl; lvl++) {
                psci_set_req_local_pwr_state(lvl, cpu_idx,
                                             state_info->pwr_domain_state[lvl]);
                state_info->pwr_domain_state[lvl] = PSCI_LOCAL_STATE_RUN;

        }

        /* Update the target state in the power domain nodes */
        psci_set_target_local_pwr_states(end_pwrlvl, state_info);
}

/******************************************************************************
 * This function ensures that the power state parameter in a CPU_SUSPEND request
 * is valid. If so, it returns the requested states for each power level.
 *****************************************************************************/
int psci_validate_power_state(unsigned int power_state,
                              psci_power_state_t *state_info)
{
        /* Check SBZ bits in power state are zero */
        if (psci_check_power_state(power_state) != 0U)
                return PSCI_E_INVALID_PARAMS;

        if (psci_plat_pm_ops->validate_power_state == NULL) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        /* Validate the power_state using platform pm_ops */
        return psci_plat_pm_ops->validate_power_state(power_state, state_info);
}

/******************************************************************************
 * This functions finds the level of the highest power domain which will be
 * placed in a low power state during a suspend operation.
 *****************************************************************************/
unsigned int psci_find_target_suspend_lvl(const psci_power_state_t *state_info)
{
        int i;

        for (i = (int) PLAT_MAX_PWR_LVL; i >= (int) PSCI_CPU_PWR_LVL; i--) {
                if (is_local_state_run(state_info->pwr_domain_state[i]) == 0)
                        return (unsigned int) i;
        }

        return PSCI_INVALID_PWR_LVL;
}

/******************************************************************************
 * This function validates a suspend request by making sure that if a standby
 * state is requested then no power level is turned off and the highest power
 * level is placed in a standby/retention state.
 *
 * It also ensures that the state level X will enter is not shallower than the
 * state level X + 1 will enter.
 *
 * This validation will be enabled only for DEBUG builds as the platform is
 * expected to perform these validations as well.
 *****************************************************************************/
int psci_validate_suspend_req(const psci_power_state_t *state_info,
                              unsigned int is_power_down_state)
{
        unsigned int max_off_lvl, target_lvl, max_retn_lvl;
        plat_local_state_t state;
        plat_local_state_type_t req_state_type, deepest_state_type;
        int i;

        /* Find the target suspend power level */
        target_lvl = psci_find_target_suspend_lvl(state_info);
        if (target_lvl == PSCI_INVALID_PWR_LVL)
                return PSCI_E_INVALID_PARAMS;

        /* All power domain levels are in a RUN state to begin with */
        deepest_state_type = STATE_TYPE_RUN;

        for (i = (int) target_lvl; i >= (int) PSCI_CPU_PWR_LVL; i--) {
                state = state_info->pwr_domain_state[i];
                req_state_type = find_local_state_type(state);

                /*
                 * While traversing from the highest power level to the lowest,
                 * the state requested for lower levels has to be the same or
                 * deeper i.e. equal to or greater than the state at the higher
                 * levels. If this condition is true, then the requested state
                 * becomes the deepest state encountered so far.
                 */
                if (req_state_type < deepest_state_type)
                        return PSCI_E_INVALID_PARAMS;
                deepest_state_type = req_state_type;
        }

        /* Find the highest off power level */
        max_off_lvl = psci_find_max_off_lvl(state_info);

        /* The target_lvl is either equal to the max_off_lvl or max_retn_lvl */
        max_retn_lvl = PSCI_INVALID_PWR_LVL;
        if (target_lvl != max_off_lvl)
                max_retn_lvl = target_lvl;

        /*
         * If this is not a request for a power down state then max off level
         * has to be invalid and max retention level has to be a valid power
         * level.
         */
        if ((is_power_down_state == 0U) &&
                        ((max_off_lvl != PSCI_INVALID_PWR_LVL) ||
                         (max_retn_lvl == PSCI_INVALID_PWR_LVL)))
                return PSCI_E_INVALID_PARAMS;

        return PSCI_E_SUCCESS;
}

void riscv_pwr_state_to_psci(unsigned int rstate, unsigned int *pstate)
{
	*pstate = 0;

	/* suspend ? */
	if (rstate & (1 << RSTATE_TYPE_SHIFT))
		*pstate |= (1 << PSTATE_TYPE_SHIFT);

	/* cluster ? */
	if (rstate & (PSTATE_PWR_LVL_MASK << RSTATE_PWR_LVL_SHIFT))
		*pstate |= (rstate & (PSTATE_PWR_LVL_MASK << RSTATE_PWR_LVL_SHIFT));
}

/*******************************************************************************
 * This function verifies that all the other cores in the system have been
 * turned OFF and the current CPU is the last running CPU in the system.
 * Returns true, if the current CPU is the last ON CPU or false otherwise.
 ******************************************************************************/
bool psci_is_last_on_cpu(void)
{
	unsigned int cpu_idx;
	unsigned int hartid = current_hartid();
	int my_idx = plat_core_pos_by_mpidr(hartid);

	for (cpu_idx = 0; cpu_idx < psci_plat_core_count; cpu_idx++) {
		if (cpu_idx == my_idx) {
			if (psci_get_aff_info_state() != AFF_STATE_ON) {
				sbi_printf("%s:%d\n", __func__, __LINE__);
				sbi_hart_hang();
			}
			continue;
		}

		if (psci_get_aff_info_state_by_idx(cpu_idx) != AFF_STATE_OFF) {
			sbi_printf("core=%u other than current core=%u %s\n",
				cpu_idx, my_idx, "running in the system");
			return false;
		}
	}

	return true;
}

/******************************************************************************
 * This function retrieves the `psci_power_state_t` for system suspend from
 * the platform.
 *****************************************************************************/
void psci_query_sys_suspend_pwrstate(psci_power_state_t *state_info)
{
	/*
	 * Assert that the required pm_ops hook is implemented to ensure that
	 * the capability detected during psci_setup() is valid.
	 */
	if (psci_plat_pm_ops->get_sys_suspend_power_state == NULL) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	/*
	 * Query the platform for the power_state required for system suspend
	 */
	psci_plat_pm_ops->get_sys_suspend_power_state(state_info);
}

