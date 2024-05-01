/*
 * Copyright (c) 2015-2022, Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include <sbi/riscv_asm.h>
#include <sbi_utils/psci/psci.h>
#include <sbi_utils/cci/cci.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_scratch.h>
#include <sbi_utils/irqchip/fdt_irqchip_plic.h>
#include <sbi_utils/psci/plat/arm/common/arm_def.h>
#include <sbi_utils/psci/plat/arm/css/common/css_pm.h>
#include <sbi_utils/psci/drivers/arm/css/css_scp.h>
#include <sbi_utils/psci/plat/arm/common/plat_arm.h>

/* Allow CSS platforms to override `plat_arm_psci_pm_ops` */
#pragma weak plat_arm_psci_pm_ops

/*******************************************************************************
 * Handler called when a power domain is about to be turned on. The
 * level and mpidr determine the affinity instance.
 ******************************************************************************/
int css_pwr_domain_on(u_register_t mpidr)
{
	css_scp_on(mpidr);

	return PSCI_E_SUCCESS;
}

static void css_pwr_domain_on_finisher_common(
		const psci_power_state_t *target_state)
{
	unsigned int clusterid;
	unsigned int hartid = current_hartid();

	if (CSS_CORE_PWR_STATE(target_state) != ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	/*
	 * Perform the common cluster specific operations i.e enable coherency
	 * if this cluster was off.
	 */
	if (CSS_CLUSTER_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		clusterid = MPIDR_AFFLVL1_VAL(hartid);
		cci_enable_snoop_dvm_reqs(clusterid);
	}
}

/*******************************************************************************
 * Handler called when a power level has just been powered on after
 * being turned off earlier. The target_state encodes the low power state that
 * each level has woken up from. This handler would never be invoked with
 * the system power domain uninitialized as either the primary would have taken
 * care of it as part of cold boot or the first core awakened from system
 * suspend would have already initialized it.
 ******************************************************************************/
void css_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
	/* Assert that the system power domain need not be initialized */
	if (css_system_pwr_state(target_state) != ARM_LOCAL_STATE_RUN) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	css_pwr_domain_on_finisher_common(target_state);
}

/*******************************************************************************
 * Handler called when a power domain has just been powered on and the cpu
 * and its cluster are fully participating in coherent transaction on the
 * interconnect. Data cache must be enabled for CPU at this point.
 ******************************************************************************/
void css_pwr_domain_on_finish_late(const psci_power_state_t *target_state)
{
#if 0
	/* Program the gic per-cpu distributor or re-distributor interface */
	plat_arm_gic_pcpu_init();

	/* Enable the gic cpu interface */
	plat_arm_gic_cpuif_enable();

	/* Setup the CPU power down request interrupt for secondary core(s) */
	css_setup_cpu_pwr_down_intr();
#endif
}

/*******************************************************************************
 * Common function called while turning a cpu off or suspending it. It is called
 * from css_off() or css_suspend() when these functions in turn are called for
 * power domain at the highest power level which will be powered down. It
 * performs the actions common to the OFF and SUSPEND calls.
 ******************************************************************************/
static void css_power_down_common(const psci_power_state_t *target_state)
{
	unsigned int clusterid;
	unsigned int hartid = current_hartid();
#if 0
	/* Prevent interrupts from spuriously waking up this cpu */
	plat_arm_gic_cpuif_disable();
#endif
	/* Cluster is to be turned off, so disable coherency */
	if (CSS_CLUSTER_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		clusterid = MPIDR_AFFLVL1_VAL(hartid);
		cci_disable_snoop_dvm_reqs(clusterid);
	}
}

static int css_pwr_domain_off_early(const psci_power_state_t *target_state)
{
	/* the ipi's pending is cleared before */
	csr_clear(CSR_MIE, MIP_SSIP | MIP_MSIP | MIP_STIP | MIP_MTIP | MIP_SEIP | MIP_MEIP);
	/* clear the external irq pending */
	csr_clear(CSR_MIP, MIP_MEIP);
	csr_clear(CSR_MIP, MIP_SEIP);

	/* here we clear the sstimer pending if this core have */
	if (sbi_hart_has_extension(sbi_scratch_thishart_ptr(), SBI_HART_EXT_SSTC)) {
		csr_write(CSR_STIMECMP, 0xffffffffffffffff);
	}

	return 0;
}

/*******************************************************************************
 * Handler called when a power domain is about to be turned off. The
 * target_state encodes the power state that each level should transition to.
 ******************************************************************************/
void css_pwr_domain_off(const psci_power_state_t *target_state)
{
	if (CSS_CORE_PWR_STATE(target_state) != ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	css_power_down_common(target_state);
	css_scp_off(target_state);
}

void css_pwr_down_wfi(const psci_power_state_t *target_state)
{
	while (1)
		wfi();
}

/*
 * The system power domain suspend is only supported only via
 * PSCI SYSTEM_SUSPEND API. PSCI CPU_SUSPEND request to system power domain
 * will be downgraded to the lower level.
 */
static int css_validate_power_state(unsigned int power_state,
                            psci_power_state_t *req_state)
{
        int rc;

        rc = arm_validate_power_state(power_state, req_state);

        /*
         * Ensure that we don't overrun the pwr_domain_state array in the case
         * where the platform supported max power level is less than the system
         * power level
         */

#if (PLAT_MAX_PWR_LVL == CSS_SYSTEM_PWR_DMN_LVL)

        /*
         * Ensure that the system power domain level is never suspended
         * via PSCI CPU SUSPEND API. Currently system suspend is only
         * supported via PSCI SYSTEM SUSPEND API.
         */

        req_state->pwr_domain_state[CSS_SYSTEM_PWR_DMN_LVL] =
                                                        ARM_LOCAL_STATE_RUN;
#endif

        return rc;
}

/*******************************************************************************
 * Handler called when the CPU power domain is about to enter standby.
 ******************************************************************************/
void css_cpu_standby(plat_local_state_t cpu_state)
{
        /* unsigned int scr; */

        if (cpu_state != ARM_LOCAL_STATE_RET) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	wfi();
#if 0
        scr = read_scr_el3();
        /*
         * Enable the Non secure interrupt to wake the CPU.
         * In GICv3 affinity routing mode, the non secure group1 interrupts use
         * the PhysicalFIQ at EL3 whereas in GICv2, it uses the PhysicalIRQ.
         * Enabling both the bits works for both GICv2 mode and GICv3 affinity
         * routing mode.
         */
        write_scr_el3(scr | SCR_IRQ_BIT | SCR_FIQ_BIT);
        isb();
        dsb();
        wfi();

        /*
         * Restore SCR to the original value, synchronisation of scr_el3 is
         * done by eret while el3_exit to save some execution cycles.
         */
        write_scr_el3(scr);
#endif
}

/*******************************************************************************
 * Handler called when a power domain is about to be suspended. The
 * target_state encodes the power state that each level should transition to.
 ******************************************************************************/
void css_pwr_domain_suspend(const psci_power_state_t *target_state)
{
        /*
         * CSS currently supports retention only at cpu level. Just return
         * as nothing is to be done for retention.
         */
        if (CSS_CORE_PWR_STATE(target_state) == ARM_LOCAL_STATE_RET)
                return;


        if (CSS_CORE_PWR_STATE(target_state) != ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        css_power_down_common(target_state);

	csr_clear(CSR_MIE, MIP_SSIP | MIP_MSIP | MIP_STIP | MIP_MTIP | MIP_SEIP | MIP_MEIP);

        /* Perform system domain state saving if issuing system suspend */
        if (css_system_pwr_state(target_state) == ARM_LOCAL_STATE_OFF) {
                /* arm_system_pwr_domain_save(); */

                /* Power off the Redistributor after having saved its context */
                /* plat_arm_gic_redistif_off(); */
        }

        css_scp_suspend(target_state);
}

/*******************************************************************************
 * Handler called when a power domain has just been powered on after
 * having been suspended earlier. The target_state encodes the low power state
 * that each level has woken up from.
 * TODO: At the moment we reuse the on finisher and reinitialize the secure
 * context. Need to implement a separate suspend finisher.
 ******************************************************************************/
void css_pwr_domain_suspend_finish(
                                const psci_power_state_t *target_state)
{
        /* Return as nothing is to be done on waking up from retention. */
        if (CSS_CORE_PWR_STATE(target_state) == ARM_LOCAL_STATE_RET)
                return;

        /* Perform system domain restore if woken up from system suspend */
        if (css_system_pwr_state(target_state) == ARM_LOCAL_STATE_OFF)
                /*
                 * At this point, the Distributor must be powered on to be ready
                 * to have its state restored. The Redistributor will be powered
                 * on as part of gicv3_rdistif_init_restore.
                 */
                /* arm_system_pwr_domain_resume() */;

        css_pwr_domain_on_finisher_common(target_state);

        /* Enable the gic cpu interface */
        /* plat_arm_gic_cpuif_enable() */;
}

/*******************************************************************************
 * Export the platform handlers via plat_arm_psci_pm_ops. The ARM Standard
 * platform will take care of registering the handlers with PSCI.
 ******************************************************************************/
plat_psci_ops_t plat_arm_psci_pm_ops = {
	.pwr_domain_on		= css_pwr_domain_on,
	.pwr_domain_on_finish	= css_pwr_domain_on_finish,
	.pwr_domain_on_finish_late = css_pwr_domain_on_finish_late,
	.pwr_domain_off		= css_pwr_domain_off,
	.pwr_domain_off_early	= css_pwr_domain_off_early,
	.pwr_domain_pwr_down_wfi = css_pwr_down_wfi,
	.validate_power_state = css_validate_power_state,
	.cpu_standby            = css_cpu_standby,
	.pwr_domain_suspend     = css_pwr_domain_suspend,
	.pwr_domain_suspend_finish      = css_pwr_domain_suspend_finish,
};
