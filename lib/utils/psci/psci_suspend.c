#include <sbi_utils/psci/psci.h>
#include <sbi_utils/cache/cacheflush.h>
#include <sbi_utils/psci/plat/common/platform.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include "psci_private.h"

/*******************************************************************************
 * This function does generic and platform specific operations after a wake-up
 * from standby/retention states at multiple power levels.
 ******************************************************************************/
static void psci_suspend_to_standby_finisher(unsigned int cpu_idx,
                                             unsigned int end_pwrlvl)
{
        unsigned int parent_nodes[PLAT_MAX_PWR_LVL] = {0};
        psci_power_state_t state_info;

        /* Get the parent nodes */
        psci_get_parent_pwr_domain_nodes(cpu_idx, end_pwrlvl, parent_nodes);

        psci_acquire_pwr_domain_locks(end_pwrlvl, parent_nodes);

        /*
         * Find out which retention states this CPU has exited from until the
         * 'end_pwrlvl'. The exit retention state could be deeper than the entry
         * state as a result of state coordination amongst other CPUs post wfi.
         */
        psci_get_target_local_pwr_states(end_pwrlvl, &state_info);

#if ENABLE_PSCI_STAT
        plat_psci_stat_accounting_stop(&state_info);
        psci_stats_update_pwr_up(end_pwrlvl, &state_info);
#endif

        /*
         * Plat. management: Allow the platform to do operations
         * on waking up from retention.
         */
        psci_plat_pm_ops->pwr_domain_suspend_finish(&state_info);

        /*
         * Set the requested and target state of this CPU and all the higher
         * power domain levels for this CPU to run.
         */
        psci_set_pwr_domains_to_run(end_pwrlvl);

        psci_release_pwr_domain_locks(end_pwrlvl, parent_nodes);
}

/*******************************************************************************
 * This function does generic and platform specific suspend to power down
 * operations.
 ******************************************************************************/
static void psci_suspend_to_pwrdown_start(unsigned int end_pwrlvl,
                                          /* const entry_point_info_t *ep */ uintptr_t ep,
                                          const psci_power_state_t *state_info)
{
	unsigned int hartid = current_hartid();
	psci_cpu_data_t *svc_cpu_data;
        /* unsigned int max_off_lvl = psci_find_max_off_lvl(state_info); */
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);
	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

	/* save something ???? */
        /* PUBLISH_EVENT(psci_suspend_pwrdown_start); */

        /* Save PSCI target power level for the suspend finisher handler */
        psci_set_suspend_pwrlvl(end_pwrlvl);

        /*
         * Flush the target power level as it might be accessed on power up with
         * Data cache disabled.
         */
	csi_dcache_clean_invalid_range((uintptr_t)&svc_cpu_data->target_pwrlvl, sizeof(unsigned int));

#if 0
        /*
         * Call the cpu suspend handler registered by the Secure Payload
         * Dispatcher to let it do any book-keeping. If the handler encounters an
         * error, it's expected to assert within
         */
        if ((psci_spd_pm != NULL) && (psci_spd_pm->svc_suspend != NULL))
                psci_spd_pm->svc_suspend(max_off_lvl);
#endif

        /*
         * Plat. management: Allow the platform to perform any early
         * actions required to power down the CPU. This might be useful for
         * HW_ASSISTED_COHERENCY = 0 platforms that can safely perform these
         * actions with data caches enabled.
         */
        if (psci_plat_pm_ops->pwr_domain_suspend_pwrdown_early != NULL)
                psci_plat_pm_ops->pwr_domain_suspend_pwrdown_early(state_info);

        /*
         * Store the re-entry information for the non-secure world.
         */
        /* cm_init_my_context(ep); */

        /*
         * Arch. management. Initiate power down sequence.
         * TODO : Introduce a mechanism to query the cache level to flush
         * and the cpu-ops power down to perform from the platform.
         */
        /* psci_pwrdown_cpu(max_off_lvl); */
	psci_do_pwrdown_cache_maintenance(hartid, (uintptr_t)scratch, psci_find_max_off_lvl(state_info));
}

/*******************************************************************************
 * Top level handler which is called when a cpu wants to suspend its execution.
 * It is assumed that along with suspending the cpu power domain, power domains
 * at higher levels until the target power level will be suspended as well. It
 * coordinates with the platform to negotiate the target state for each of
 * the power domain level till the target power domain level. It then performs
 * generic, architectural, platform setup and state management required to
 * suspend that power domain level and power domain levels below it.
 * e.g. For a cpu that's to be suspended, it could mean programming the
 * power controller whereas for a cluster that's to be suspended, it will call
 * the platform specific code which will disable coherency at the interconnect
 * level if the cpu is the last in the cluster and also the program the power
 * controller.
 *
 * All the required parameter checks are performed at the beginning and after
 * the state transition has been done, no further error is expected and it is
 * not possible to undo any of the actions taken beyond that point.
 ******************************************************************************/
int psci_cpu_suspend_start(/* const entry_point_info_t *ep */uintptr_t ep,
                           unsigned int end_pwrlvl,
                           psci_power_state_t *state_info,
                           unsigned int is_power_down_state)
{
        int rc = PSCI_E_SUCCESS;
        bool skip_wfi = false;
	unsigned int hartid = current_hartid();
        unsigned int idx = plat_core_pos_by_mpidr(hartid);
        unsigned int parent_nodes[PLAT_MAX_PWR_LVL] = {0};

        /*
         * This function must only be called on platforms where the
         * CPU_SUSPEND platform hooks have been implemented.
         */
        if ((psci_plat_pm_ops->pwr_domain_suspend == NULL) ||
               (psci_plat_pm_ops->pwr_domain_suspend_finish == NULL)) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        /* Get the parent nodes */
        psci_get_parent_pwr_domain_nodes(idx, end_pwrlvl, parent_nodes);

        /*
         * This function acquires the lock corresponding to each power
         * level so that by the time all locks are taken, the system topology
         * is snapshot and state management can be done safely.
         */
        psci_acquire_pwr_domain_locks(end_pwrlvl, parent_nodes);

        /*
         * We check if there are any pending interrupts after the delay
         * introduced by lock contention to increase the chances of early
         * detection that a wake-up interrupt has fired.
         */
        if (__get_Supervisor_isr() != 0U) {
                skip_wfi = true;
                goto exit;
        }

        /*
         * This function is passed the requested state info and
         * it returns the negotiated state info for each power level upto
         * the end level specified.
         */
        psci_do_state_coordination(end_pwrlvl, state_info);

#if ENABLE_PSCI_STAT
        /* Update the last cpu for each level till end_pwrlvl */
        psci_stats_update_pwr_down(end_pwrlvl, state_info);
#endif

        if (is_power_down_state != 0U)
                psci_suspend_to_pwrdown_start(end_pwrlvl, ep, state_info);

        /*
         * Plat. management: Allow the platform to perform the
         * necessary actions to turn off this cpu e.g. set the
         * platform defined mailbox with the psci entrypoint,
         * program the power controller etc.
         */

        psci_plat_pm_ops->pwr_domain_suspend(state_info);

#if ENABLE_PSCI_STAT
        plat_psci_stat_accounting_start(state_info);
#endif

exit:
        /*
         * Release the locks corresponding to each power level in the
         * reverse order to which they were acquired.
         */
        psci_release_pwr_domain_locks(end_pwrlvl, parent_nodes);

        if (skip_wfi) {
                return rc;
        }

        if (is_power_down_state != 0U) {
                /* The function calls below must not return */
                if (psci_plat_pm_ops->pwr_domain_pwr_down_wfi != NULL)
                        psci_plat_pm_ops->pwr_domain_pwr_down_wfi(state_info);
                else
                        /* psci_power_down_wfi() */;
        }

        /*
         * We will reach here if only retention/standby states have been
         * requested at multiple power levels. This means that the cpu
         * context will be preserved.
         */
        /* wfi(); */
	asm volatile ("wfi");

        /*
         * After we wake up from context retaining suspend, call the
         * context retaining suspend finisher.
         */
        psci_suspend_to_standby_finisher(idx, end_pwrlvl);

        return rc;
}

/*******************************************************************************
 * The following functions finish an earlier suspend request. They
 * are called by the common finisher routine in psci_common.c. The `state_info`
 * is the psci_power_state from which this CPU has woken up from.
 ******************************************************************************/
void psci_cpu_suspend_finish(unsigned int cpu_idx, const psci_power_state_t *state_info)
{
        /* unsigned int counter_freq; */
        /* unsigned int max_off_lvl; */
	unsigned int hartid = current_hartid();
	psci_cpu_data_t *svc_cpu_data;
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(hartid);
	svc_cpu_data = sbi_scratch_offset_ptr(scratch, psci_delta_off);

        /* Ensure we have been woken up from a suspended state */
        if ((psci_get_aff_info_state() != AFF_STATE_ON) ||
                (is_local_state_off(
                        state_info->pwr_domain_state[PSCI_CPU_PWR_LVL]) == 0)) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        /*
         * Plat. management: Perform the platform specific actions
         * before we change the state of the cpu e.g. enabling the
         * gic or zeroing the mailbox register. If anything goes
         * wrong then assert as there is no way to recover from this
         * situation.
         */
        psci_plat_pm_ops->pwr_domain_suspend_finish(state_info);

        /* Arch. management: Enable the data cache, stack memory maintenance. */
        psci_do_pwrup_cache_maintenance((uintptr_t)scratch);

#if 0
        /* Re-init the cntfrq_el0 register */
        counter_freq = plat_get_syscnt_freq2();
        write_cntfrq_el0(counter_freq);
#endif
        /*
         * Call the cpu suspend finish handler registered by the Secure Payload
         * Dispatcher to let it do any bookeeping. If the handler encounters an
         * error, it's expected to assert within
         */
#if 0
        if ((psci_spd_pm != NULL) && (psci_spd_pm->svc_suspend_finish != NULL)) {
                max_off_lvl = psci_find_max_off_lvl(state_info);
                assert(max_off_lvl != PSCI_INVALID_PWR_LVL);
                psci_spd_pm->svc_suspend_finish(max_off_lvl);
        }
#endif

        /* Invalidate the suspend level for the cpu */
        psci_set_suspend_pwrlvl(PSCI_INVALID_PWR_LVL);
	csi_dcache_clean_invalid_range((uintptr_t)&svc_cpu_data->target_pwrlvl, sizeof(unsigned int));

        /* PUBLISH_EVENT(psci_suspend_pwrdown_finish); */

        /*
         * Generic management: Now we just need to retrieve the
         * information that we had stashed away during the suspend
         * call to set this cpu on its way.
         */
        /* cm_prepare_el3_exit_ns(); */
}
