/*
 * Copyright (c) 2013-2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sbi/sbi_console.h>
#include <sbi_utils/cache/cacheflush.h>
#include <sbi_utils/psci/plat/common/platform.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_scratch.h>
#include <sbi_utils/psci/psci_lib.h>
#include "psci_private.h"

/*******************************************************************************
 * Function which initializes the 'psci_non_cpu_pd_nodes' or the
 * 'psci_cpu_pd_nodes' corresponding to the power level.
 ******************************************************************************/
static void psci_init_pwr_domain_node(uint16_t node_idx,
					unsigned int parent_idx,
					unsigned char level)
{
	if (level > PSCI_CPU_PWR_LVL) {
		if (node_idx >= PSCI_NUM_NON_CPU_PWR_DOMAINS) {
			sbi_printf("%s:%d, node_idx beyond the boundary\n",
					__func__, __LINE__);
			sbi_hart_hang();
		}


		psci_non_cpu_pd_nodes[node_idx].level = level;
		psci_lock_init(psci_non_cpu_pd_nodes, node_idx);
		psci_non_cpu_pd_nodes[node_idx].parent_node = parent_idx;
		psci_non_cpu_pd_nodes[node_idx].local_state =
							 PLAT_MAX_OFF_STATE;
	} else {
		psci_cpu_data_t *svc_cpu_data;
		const struct sbi_platform *sbi = sbi_platform_thishart_ptr();

		if (node_idx >= PLATFORM_CORE_COUNT) {
			sbi_printf("%s:%d, node_idx beyond the boundary\n",
					__func__, __LINE__);
			sbi_hart_hang();
		}

		unsigned int hartid = sbi->hart_index2id[node_idx];

		psci_cpu_pd_nodes[node_idx].parent_node = parent_idx;

		/* Initialize with an invalid mpidr */
		psci_cpu_pd_nodes[node_idx].mpidr = PSCI_INVALID_MPIDR;

		struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);

		svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

		/* Set the Affinity Info for the cores as OFF */
		svc_cpu_data->aff_info_state = AFF_STATE_OFF;

		/* Invalidate the suspend level for the cpu */
		svc_cpu_data->target_pwrlvl = PSCI_INVALID_PWR_LVL;

		/* Set the power state to OFF state */
		svc_cpu_data->local_state = PLAT_MAX_OFF_STATE;

		csi_dcache_clean_invalid_range((uintptr_t)svc_cpu_data, sizeof(psci_cpu_data_t));
	}
}

/*******************************************************************************
 * This functions updates cpu_start_idx and ncpus field for each of the node in
 * psci_non_cpu_pd_nodes[]. It does so by comparing the parent nodes of each of
 * the CPUs and check whether they match with the parent of the previous
 * CPU. The basic assumption for this work is that children of the same parent
 * are allocated adjacent indices. The platform should ensure this though proper
 * mapping of the CPUs to indices via plat_core_pos_by_mpidr() and
 * plat_my_core_pos() APIs.
 *******************************************************************************/
static void psci_update_pwrlvl_limits(void)
{
	unsigned int cpu_idx;
	int j;
	unsigned int nodes_idx[PLAT_MAX_PWR_LVL] = {0};
	unsigned int temp_index[PLAT_MAX_PWR_LVL];

	for (cpu_idx = 0; cpu_idx < psci_plat_core_count; cpu_idx++) {
		psci_get_parent_pwr_domain_nodes(cpu_idx,
						 PLAT_MAX_PWR_LVL,
						 temp_index);
		for (j = (int)PLAT_MAX_PWR_LVL - 1; j >= 0; j--) {
			if (temp_index[j] != nodes_idx[j]) {
				nodes_idx[j] = temp_index[j];
				psci_non_cpu_pd_nodes[nodes_idx[j]].cpu_start_idx
					= cpu_idx;
			}
			psci_non_cpu_pd_nodes[nodes_idx[j]].ncpus++;
		}
	}
}

/*******************************************************************************
 * Core routine to populate the power domain tree. The tree descriptor passed by
 * the platform is populated breadth-first and the first entry in the map
 * informs the number of root power domains. The parent nodes of the root nodes
 * will point to an invalid entry(-1).
 ******************************************************************************/
static unsigned int populate_power_domain_tree(const unsigned char
							*topology)
{
	unsigned int i, j = 0U, num_nodes_at_lvl = 1U, num_nodes_at_next_lvl;
	unsigned int node_index = 0U, num_children;
	unsigned int parent_node_index = 0U;
	int level = (int)PLAT_MAX_PWR_LVL;

	/*
	 * For each level the inputs are:
	 * - number of nodes at this level in plat_array i.e. num_nodes_at_level
	 *   This is the sum of values of nodes at the parent level.
	 * - Index of first entry at this level in the plat_array i.e.
	 *   parent_node_index.
	 * - Index of first free entry in psci_non_cpu_pd_nodes[] or
	 *   psci_cpu_pd_nodes[] i.e. node_index depending upon the level.
	 */
	while (level >= (int) PSCI_CPU_PWR_LVL) {
		num_nodes_at_next_lvl = 0U;
		/*
		 * For each entry (parent node) at this level in the plat_array:
		 * - Find the number of children
		 * - Allocate a node in a power domain array for each child
		 * - Set the parent of the child to the parent_node_index - 1
		 * - Increment parent_node_index to point to the next parent
		 * - Accumulate the number of children at next level.
		 */
		for (i = 0U; i < num_nodes_at_lvl; i++) {
			if (parent_node_index > PSCI_NUM_NON_CPU_PWR_DOMAINS) {
				sbi_printf("%s:%d, node_idx beyond the boundary\n",
						__func__, __LINE__);
				sbi_hart_hang();
			}

			num_children = topology[parent_node_index];

			for (j = node_index;
				j < (node_index + num_children); j++)
				psci_init_pwr_domain_node((uint16_t)j,
						  parent_node_index - 1U,
						  (unsigned char)level);

			node_index = j;
			num_nodes_at_next_lvl += num_children;
			parent_node_index++;
		}

		num_nodes_at_lvl = num_nodes_at_next_lvl;
		level--;

		/* Reset the index for the cpu power domain array */
		if (level == (int) PSCI_CPU_PWR_LVL)
			node_index = 0;
	}

	/* Validate the sanity of array exported by the platform */
	if (j > PLATFORM_CORE_COUNT) {
		sbi_printf("%s:%d, invalidate core count\n",
				__func__, __LINE__);
		sbi_hart_hang();
	}
	return j;
}

/*******************************************************************************
 * This function does the architectural setup and takes the warm boot
 * entry-point `mailbox_ep` as an argument. The function also initializes the
 * power domain topology tree by querying the platform. The power domain nodes
 * higher than the CPU are populated in the array psci_non_cpu_pd_nodes[] and
 * the CPU power domains are populated in psci_cpu_pd_nodes[]. The platform
 * exports its static topology map through the
 * populate_power_domain_topology_tree() API. The algorithm populates the
 * psci_non_cpu_pd_nodes and psci_cpu_pd_nodes iteratively by using this
 * topology map.  On a platform that implements two clusters of 2 cpus each,
 * and supporting 3 domain levels, the populated psci_non_cpu_pd_nodes would
 * look like this:
 *
 * ---------------------------------------------------
 * | system node | cluster 0 node  | cluster 1 node  |
 * ---------------------------------------------------
 *
 * And populated psci_cpu_pd_nodes would look like this :
 * <-    cpus cluster0   -><-   cpus cluster1   ->
 * ------------------------------------------------
 * |   CPU 0   |   CPU 1   |   CPU 2   |   CPU 3  |
 * ------------------------------------------------
 ******************************************************************************/
int psci_setup(void)
{
	unsigned int cpu_idx;
	const unsigned char *topology_tree;
	unsigned int hartid = current_hartid();

	cpu_idx = plat_core_pos_by_mpidr(hartid); 

	psci_delta_off = sbi_scratch_alloc_offset(sizeof(psci_cpu_data_t));
	if (!psci_delta_off)
		return SBI_ENOMEM;

	/* Query the topology map from the platform */
	topology_tree = plat_get_power_domain_tree_desc();

	/* Populate the power domain arrays using the platform topology map */
	psci_plat_core_count = populate_power_domain_tree(topology_tree);

	/* Update the CPU limits for each node in psci_non_cpu_pd_nodes */
	psci_update_pwrlvl_limits();

	/* Populate the mpidr field of cpu node for this CPU */
	psci_cpu_pd_nodes[cpu_idx].mpidr = hartid;

	psci_init_req_local_pwr_states();

	/*
	 * Set the requested and target state of this CPU and all the higher
	 * power domain levels for this CPU to run.
	 */
	psci_set_pwr_domains_to_run(PLAT_MAX_PWR_LVL);

	psci_print_power_domain_map();

	(void) plat_setup_psci_ops(0, &psci_plat_pm_ops);
	if (psci_plat_pm_ops == NULL) {
		sbi_printf("%s:%d, invalid psci ops\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	/*
         * Flush `psci_plat_pm_ops` as it will be accessed by secondary CPUs
         * during warm boot, possibly before data cache is enabled.
         */
	csi_dcache_clean_invalid_range((uintptr_t)&psci_plat_pm_ops, sizeof(*psci_plat_pm_ops));

	return 0;
}
