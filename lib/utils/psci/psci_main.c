#include <sbi_utils/psci/psci.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_types.h>
#include <sbi/sbi_scratch.h>
#include "psci_private.h"

/*******************************************************************************
 * PSCI frontend api for servicing SMCs. Described in the PSCI spec.
 ******************************************************************************/
int psci_cpu_on(u_register_t target_cpu, uintptr_t entrypoint)
{
	int rc;

	/* Determine if the cpu exists of not */
	rc = psci_validate_mpidr(target_cpu);
	if (rc != PSCI_E_SUCCESS)
		return PSCI_E_INVALID_PARAMS;

	/*
	 * To turn this cpu on, specify which power
	 * levels need to be turned on
	 */
	return psci_cpu_on_start(target_cpu, entrypoint);
}

int psci_affinity_info(u_register_t target_affinity,
                       unsigned int lowest_affinity_level)
{
	int ret;
	unsigned int target_idx;
	psci_cpu_data_t *svc_cpu_data;
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(target_affinity);
	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	/* We dont support level higher than PSCI_CPU_PWR_LVL */
        if (lowest_affinity_level > PSCI_CPU_PWR_LVL)
                return PSCI_E_INVALID_PARAMS;

        /* Calculate the cpu index of the target */
        ret = plat_core_pos_by_mpidr(target_affinity);
        if (ret == -1) {
                return PSCI_E_INVALID_PARAMS;
        }
        target_idx = (unsigned int)ret;

        /*
         * Generic management:
         * Perform cache maintanence ahead of reading the target CPU state to
         * ensure that the data is not stale.
         * There is a theoretical edge case where the cache may contain stale
         * data for the target CPU data - this can occur under the following
         * conditions:
         * - the target CPU is in another cluster from the current
         * - the target CPU was the last CPU to shutdown on its cluster
         * - the cluster was removed from coherency as part of the CPU shutdown
         *
         * In this case the cache maintenace that was performed as part of the
         * target CPUs shutdown was not seen by the current CPU's cluster. And
         * so the cache may contain stale data for the target CPU.
         */
	csi_dcache_clean_invalid_range((uintptr_t)&svc_cpu_data->aff_info_state, sizeof(aff_info_state_t));

        return psci_get_aff_info_state_by_idx(target_idx);
}

int psci_cpu_off(void)
{
        int rc;
        unsigned int target_pwrlvl = PLAT_MAX_PWR_LVL;

        /*
         * Do what is needed to power off this CPU and possible higher power
         * levels if it able to do so. Upon success, enter the final wfi
         * which will power down this CPU.
         */
        rc = psci_do_cpu_off(target_pwrlvl);

        /*
         * The only error cpu_off can return is E_DENIED. So check if that's
         * indeed the case.
         */
        if (rc != PSCI_E_DENIED) {
		sbi_printf("%s:%d, err\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        return rc;
}

int psci_cpu_suspend(unsigned int power_state,
		     uintptr_t entrypoint,
		     u_register_t context_id)
{
	int rc;
	unsigned int target_pwrlvl, is_power_down_state, pwr_state;
	/* entry_point_info_t ep; */
	psci_power_state_t state_info = { {PSCI_LOCAL_STATE_RUN} };
	plat_local_state_t cpu_pd_state;

	riscv_pwr_state_to_psci(power_state, &pwr_state);

	/* Validate the power_state parameter */
	rc = psci_validate_power_state(pwr_state, &state_info);
	if (rc != PSCI_E_SUCCESS) {
		if (rc != PSCI_E_INVALID_PARAMS) {
			sbi_printf("%s:%d\n", __func__, __LINE__);
			sbi_hart_hang();
		}
		return rc;
	}

	/*
	 * Get the value of the state type bit from the power state parameter.
	 */
	is_power_down_state = psci_get_pstate_type(pwr_state);

	/* Sanity check the requested suspend levels */
	if (psci_validate_suspend_req(&state_info, is_power_down_state)
			!= PSCI_E_SUCCESS) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	target_pwrlvl = psci_find_target_suspend_lvl(&state_info);
	if (target_pwrlvl == PSCI_INVALID_PWR_LVL) {
		sbi_printf("Invalid target power level for suspend operation\n");
		sbi_hart_hang();
	}

	/* Fast path for CPU standby.*/
	if (is_cpu_standby_req(is_power_down_state, target_pwrlvl)) {
		if  (psci_plat_pm_ops->cpu_standby == NULL)
			return PSCI_E_INVALID_PARAMS;

		/*
		 * Set the state of the CPU power domain to the platform
		 * specific retention state and enter the standby state.
		 */
		cpu_pd_state = state_info.pwr_domain_state[PSCI_CPU_PWR_LVL];
		psci_set_cpu_local_state(cpu_pd_state);

#if ENABLE_PSCI_STAT
		plat_psci_stat_accounting_start(&state_info);
#endif

		psci_plat_pm_ops->cpu_standby(cpu_pd_state);

		/* Upon exit from standby, set the state back to RUN. */
		psci_set_cpu_local_state(PSCI_LOCAL_STATE_RUN);

#if ENABLE_PSCI_STAT
		plat_psci_stat_accounting_stop(&state_info);

		/* Update PSCI stats */
		psci_stats_update_pwr_up(PSCI_CPU_PWR_LVL, &state_info);
#endif

		return PSCI_E_SUCCESS;
	}

	/*
	 * If a power down state has been requested, we need to verify entry
	 * point and program entry information.
	 */
	if (is_power_down_state != 0U) {
	/*	rc = psci_validate_entry_point(&ep, entrypoint, context_id);
		if (rc != PSCI_E_SUCCESS)
			return rc; */;
	}

	/*
	 * Do what is needed to enter the power down state. Upon success,
	 * enter the final wfi which will power down this CPU. This function
	 * might return if the power down was abandoned for any reason, e.g.
	 * arrival of an interrupt
	 */
	rc = psci_cpu_suspend_start(/* &ep */entrypoint,
				    target_pwrlvl,
				    &state_info,
				    is_power_down_state);

	return rc;
}

int psci_system_suspend(uintptr_t entrypoint, u_register_t context_id)
{
	int rc;
	psci_power_state_t state_info;
	/* entry_point_info_t ep; */

	/* Check if the current CPU is the last ON CPU in the system */
	if (!psci_is_last_on_cpu())
		return PSCI_E_DENIED;

	/* Validate the entry point and get the entry_point_info */
/**
 *	rc = psci_validate_entry_point(&ep, entrypoint, context_id);
 *	if (rc != PSCI_E_SUCCESS)
 *		return rc;
 */

	/* Query the psci_power_state for system suspend */
	psci_query_sys_suspend_pwrstate(&state_info);

	/*
	 * Check if platform allows suspend to Highest power level
	 * (System level)
	 */
	if (psci_find_target_suspend_lvl(&state_info) < PLAT_MAX_PWR_LVL)
		return PSCI_E_DENIED;

	/* Ensure that the psci_power_state makes sense */
	if (psci_validate_suspend_req(&state_info, PSTATE_TYPE_POWERDOWN)
                                                != PSCI_E_SUCCESS) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	if (is_local_state_off(
		state_info.pwr_domain_state[PLAT_MAX_PWR_LVL]) == 0) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	/*
	 * Do what is needed to enter the system suspend state. This function
	 * might return if the power down was abandoned for any reason, e.g.
	 * arrival of an interrupt
	 */
	rc = psci_cpu_suspend_start(/* &ep */entrypoint,
				PLAT_MAX_PWR_LVL,
				&state_info,
				PSTATE_TYPE_POWERDOWN);

        return rc;
}
