#include <sbi/sbi_types.h>
#include <sbi/riscv_asm.h>
#include <sbi_utils/cci/cci.h>
#include <sbi_utils/psci/psci.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include <sbi_utils/psci/plat/arm/common/arm_def.h>
#include <sbi_utils/irqchip/fdt_irqchip_plic.h>
#include <sbi_utils/cache/cacheflush.h>
#include "underly_implement.h"

#define CORE_PWR_STATE(state) \
        ((state)->pwr_domain_state[MPIDR_AFFLVL0])
#define CLUSTER_PWR_STATE(state) \
        ((state)->pwr_domain_state[MPIDR_AFFLVL1])
#define SYSTEM_PWR_STATE(state) \
        ((state)->pwr_domain_state[PLAT_MAX_PWR_LVL])

/* reserved for future used */
/* unsigned long __plic_regsave_offset_ptr; */

static int spacemit_pwr_domain_on(u_register_t mpidr)
{
	/* wakeup the cpu */
	spacemit_wakeup_cpu(mpidr);

	return 0;
}

static void spacemit_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
        unsigned int hartid = current_hartid();

	if (SYSTEM_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		/* D1P */
		spacemit_top_on(hartid);
	}

        /*
         * Enable CCI coherency for this cluster.
         * No need for locks as no other cpu is active at the moment.
         */
        if (CLUSTER_PWR_STATE(target_state) == PLAT_MAX_OFF_STATE) {
                spacemit_cluster_on(hartid);
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* disable the tcm */
		csr_write(CSR_TCMCFG, 0);
#endif
                cci_enable_snoop_dvm_reqs(MPIDR_AFFLVL1_VAL(hartid));
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* enable the tcm */
		csr_write(CSR_TCMCFG, 1);
#endif
	}
}

static int spacemit_pwr_domain_off_early(const psci_power_state_t *target_state)
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

static void spacemit_pwr_domain_off(const psci_power_state_t *target_state)
{
	unsigned int hartid = current_hartid();

	if (CLUSTER_PWR_STATE(target_state) == PLAT_MAX_OFF_STATE) {
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* disable the tcm */
		csr_write(CSR_TCMCFG, 0);
#endif
                cci_disable_snoop_dvm_reqs(MPIDR_AFFLVL1_VAL(hartid));
                spacemit_cluster_off(hartid);
		csi_flush_l2_cache(1);
        }

	if (SYSTEM_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		/* D1P */
		spacemit_top_off(hartid);
	}

	spacemit_assert_cpu(hartid);
}

static void spacemit_pwr_domain_pwr_down_wfi(const psci_power_state_t *target_state)
{
	while (1) {
		asm volatile ("wfi");
	}
}

static void spacemit_pwr_domain_on_finish_late(const psci_power_state_t *target_state)
{
	spacemit_deassert_cpu();
}

static int _spacemit_validate_power_state(unsigned int power_state,
                            psci_power_state_t *req_state)
{
        unsigned int pstate = psci_get_pstate_type(power_state);
        unsigned int pwr_lvl = psci_get_pstate_pwrlvl(power_state);
        unsigned int i;

        if (req_state == NULL) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        if (pwr_lvl > PLAT_MAX_PWR_LVL)
                return PSCI_E_INVALID_PARAMS;

        /* Sanity check the requested state */
        if (pstate == PSTATE_TYPE_STANDBY) {
                /*
                 * It's possible to enter standby only on power level 0
                 * Ignore any other power level.
                 */
                if (pwr_lvl != ARM_PWR_LVL0)
                        return PSCI_E_INVALID_PARAMS;

                req_state->pwr_domain_state[ARM_PWR_LVL0] =
                                        ARM_LOCAL_STATE_RET;
        } else {
                for (i = ARM_PWR_LVL0; i <= pwr_lvl; i++)
                        req_state->pwr_domain_state[i] =
                                        ARM_LOCAL_STATE_OFF;
        }

        /*
         * We expect the 'state id' to be zero.
         */
        if (psci_get_pstate_id(power_state) != 0U)
                return PSCI_E_INVALID_PARAMS;

        return PSCI_E_SUCCESS;
}

static int spacemit_validate_power_state(unsigned int power_state,
                            psci_power_state_t *req_state)
{
        int rc;

        rc = _spacemit_validate_power_state(power_state, req_state);

        return rc;
}

static void spacemit_pwr_domain_suspend(const psci_power_state_t *target_state)
{
	unsigned int clusterid;
	unsigned int hartid = current_hartid();

        /*
         * CSS currently supports retention only at cpu level. Just return
         * as nothing is to be done for retention.
         */
        if (CORE_PWR_STATE(target_state) == ARM_LOCAL_STATE_RET)
                return;


        if (CORE_PWR_STATE(target_state) != ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	/* Cluster is to be turned off, so disable coherency */
	if (CLUSTER_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		clusterid = MPIDR_AFFLVL1_VAL(hartid);
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* disable the tcm */
		csr_write(CSR_TCMCFG, 0);
#endif
		cci_disable_snoop_dvm_reqs(clusterid);
		spacemit_cluster_off(hartid);
		csi_flush_l2_cache(1);
	}

	if (SYSTEM_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		/* D1P & D2 */
		spacemit_top_off(hartid);
	}

	spacemit_assert_cpu(hartid);
}

static void spacemit_pwr_domain_suspend_finish(const psci_power_state_t *target_state)
{
	unsigned int clusterid;
	unsigned int hartid = current_hartid();

        /* Return as nothing is to be done on waking up from retention. */
        if (CORE_PWR_STATE(target_state) == ARM_LOCAL_STATE_RET)
                return;

	if (CORE_PWR_STATE(target_state) != ARM_LOCAL_STATE_OFF) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	/*
	 * Perform the common cluster specific operations i.e enable coherency
	 * if this cluster was off.
	 */
	if (CLUSTER_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		clusterid = MPIDR_AFFLVL1_VAL(hartid);
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* disable the tcm */
		csr_write(CSR_TCMCFG, 0);
#endif
		cci_enable_snoop_dvm_reqs(clusterid);
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* enable the tcm */
		csr_write(CSR_TCMCFG, 1);
#endif
		spacemit_cluster_on(hartid);
	}

	if (SYSTEM_PWR_STATE(target_state) == ARM_LOCAL_STATE_OFF) {
		/* D1P & D2 */
		spacemit_top_on(hartid);
	}

	/* Do something */
	spacemit_deassert_cpu();
}

static void spacemit_pwr_domain_suspend_pwrdown_early(const psci_power_state_t *target_state)
{
	csr_clear(CSR_MIE, MIP_SSIP | MIP_MSIP | MIP_STIP | MIP_MTIP | MIP_SEIP | MIP_MEIP);
}

static void spacemit_get_sys_suspend_power_state(psci_power_state_t *req_state)
{
	int i;

	for (i = MPIDR_AFFLVL0; i <= PLAT_MAX_PWR_LVL; i++)
		req_state->pwr_domain_state[i] = PLAT_MAX_OFF_STATE;
}

static const plat_psci_ops_t spacemit_psci_ops = {
	.cpu_standby = NULL,
	.pwr_domain_on = spacemit_pwr_domain_on,
	.pwr_domain_on_finish = spacemit_pwr_domain_on_finish,
	.pwr_domain_off_early	= spacemit_pwr_domain_off_early,
	.pwr_domain_off = spacemit_pwr_domain_off,
	.pwr_domain_pwr_down_wfi = spacemit_pwr_domain_pwr_down_wfi,
	.pwr_domain_on_finish_late = spacemit_pwr_domain_on_finish_late,
	.validate_power_state = spacemit_validate_power_state,
	.pwr_domain_suspend = spacemit_pwr_domain_suspend,
	.pwr_domain_suspend_pwrdown_early = spacemit_pwr_domain_suspend_pwrdown_early,
	.pwr_domain_suspend_finish = spacemit_pwr_domain_suspend_finish,
	.get_sys_suspend_power_state = spacemit_get_sys_suspend_power_state,
};

int plat_setup_psci_ops(uintptr_t sec_entrypoint, const plat_psci_ops_t **psci_ops)
{
	*psci_ops = &spacemit_psci_ops;

        return 0;
}
