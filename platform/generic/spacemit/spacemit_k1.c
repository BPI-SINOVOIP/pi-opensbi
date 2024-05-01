/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Spacemit.
 */

#include <libfdt.h>
#include <platform_override.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/riscv_atomic.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/psci/psci_lib.h>
#include <sbi_utils/cci/cci.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi_utils/psci/psci.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_system.h>
#include <sbi_utils/cache/cacheflush.h>
#include <../../../lib/utils/psci/psci_private.h>
#include <sbi_utils/psci/plat/arm/common/plat_arm.h>
#include <sbi_utils/psci/plat/common/platform.h>
#include <spacemit/spacemit_config.h>

extern struct sbi_platform platform;

/* reserved for future use */
/* extern unsigned long __plic_regsave_offset_ptr; */

PLAT_CCI_MAP

static void wakeup_other_core(void)
{
	int i;
	u32 hartid, clusterid, cluster_enabled = 0;
	unsigned int cur_hartid = current_hartid();
	struct sbi_scratch *scratch = sbi_hartid_to_scratch(cur_hartid);

#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
	/* set other cpu's boot-entry */
	writel(scratch->warmboot_addr & 0xffffffff, (u32 *)C0_RVBADDR_LO_ADDR);
	writel((scratch->warmboot_addr >> 32) & 0xffffffff, (u32 *)C0_RVBADDR_HI_ADDR);

	writel(scratch->warmboot_addr & 0xffffffff, (u32 *)C1_RVBADDR_LO_ADDR);
	writel((scratch->warmboot_addr >> 32) & 0xffffffff, (u32 *)C1_RVBADDR_HI_ADDR);
#elif defined(CONFIG_PLATFORM_SPACEMIT_K1PRO)
	for (i = 0; i < platform.hart_count; i++) {
		hartid = platform.hart_index2id[i];

	unsigned long core_index = MPIDR_AFFLVL1_VAL(hartid) * PLATFORM_MAX_CPUS_PER_CLUSTER
			+ MPIDR_AFFLVL0_VAL(hartid);

	writel(scratch->warmboot_addr & 0xffffffff, (u32 *)(CORE0_RVBADDR_LO_ADDR + core_index * CORE_RVBADDR_STEP));
	writel((scratch->warmboot_addr >> 32) & 0xffffffff, (u32 *)(CORE0_RVBADDR_HI_ADDR + core_index * CORE_RVBADDR_STEP));
	}
#endif

#ifdef CONFIG_ARM_PSCI_SUPPORT
	unsigned char *cpu_topology = plat_get_power_domain_tree_desc();
#endif

#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
	/* enable the hw l2 cache flush method for each core */
	writel(readl((u32 *)PMU_C0_CAPMP_IDLE_CFG0) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C0_CAPMP_IDLE_CFG0);
	writel(readl((u32 *)PMU_C0_CAPMP_IDLE_CFG1) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C0_CAPMP_IDLE_CFG1);
	writel(readl((u32 *)PMU_C0_CAPMP_IDLE_CFG2) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C0_CAPMP_IDLE_CFG2);
	writel(readl((u32 *)PMU_C0_CAPMP_IDLE_CFG3) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C0_CAPMP_IDLE_CFG3);

	writel(readl((u32 *)PMU_C1_CAPMP_IDLE_CFG0) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C1_CAPMP_IDLE_CFG0);
	writel(readl((u32 *)PMU_C1_CAPMP_IDLE_CFG1) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C1_CAPMP_IDLE_CFG1);
	writel(readl((u32 *)PMU_C1_CAPMP_IDLE_CFG2) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C1_CAPMP_IDLE_CFG2);
	writel(readl((u32 *)PMU_C1_CAPMP_IDLE_CFG3) | (1 << L2_HARDWARE_CACHE_FLUSH_EN), (u32 *)PMU_C1_CAPMP_IDLE_CFG3);
#endif

	// hart0 is already boot up
	for (i = 0; i < platform.hart_count; i++) {
		hartid = platform.hart_index2id[i];

		clusterid = MPIDR_AFFLVL1_VAL(hartid);

		/* we only enable snoop of cluster0 */
		if (0 == (cluster_enabled & (1 << clusterid))) {
			cluster_enabled |= 1 << clusterid;
			if (0 == clusterid) {
				cci_enable_snoop_dvm_reqs(clusterid);
			}
#ifdef CONFIG_ARM_PSCI_SUPPORT
			cpu_topology[CLUSTER_INDEX_IN_CPU_TOPOLOGY]++;
#endif
		}

#ifdef CONFIG_ARM_PSCI_SUPPORT
		/* we only support 2 cluster by now */
		if (clusterid == PLATFORM_CLUSTER_COUNT - 1)
			cpu_topology[CLUSTER1_INDEX_IN_CPU_TOPOLOGY]++;
		else
			cpu_topology[CLUSTER0_INDEX_IN_CPU_TOPOLOGY]++;
#endif
	}

/**
 * // reserved for future used
 *   // get the number of plic registers
 *   u32 *regnum_pos;
 *   int noff = -1, fdtlen, regnum, regsize;
 *   const fdt32_t *fdtval;
 *   void *fdt = fdt_get_address();
 *   const struct fdt_match match_table = { .compatible = "riscv,plic0", };
 *
 *   noff = fdt_find_match(fdt, noff, &match_table, NULL);
 *   if (noff >= 0) {
 *	    fdtval = fdt_getprop(fdt, noff, "riscv,ndev", &fdtlen);
 *	    if (fdtlen > 0) {
 *		   regnum = fdt32_to_cpu(*fdtval);
 *		   regsize =
 *			   // regnum + regsize
 *			   sizeof(u32) + sizeof(u32) +
 *			   // plic priority regisrer
 *			   sizeof(u8) * regnum +
 *			   // plic enable register
 *			   (sizeof(u32) * (regnum / 32 + 1) +
 *			   // plic threshold regisrer
 *			   sizeof (u32) * 1) * 2; // smode and machine mode
 *
 *		   __plic_regsave_offset_ptr = sbi_scratch_alloc_offset(regsize);
 *		  if (__plic_regsave_offset_ptr == 0) {
 *			 sbi_hart_hang();
 *		  }
 *	    }
 *   }
 *
 *    if (__plic_regsave_offset_ptr) {
 *	    for (i = 0; i < platform.hart_count; i++) {
 *		    hartid = platform.hart_index2id[i];
 *		    scratch = sbi_hartid_to_scratch(hartid);
 *		    u32 *regnum_pos = sbi_scratch_offset_ptr(scratch, __plic_regsave_offset_ptr);
 *
 *		    regnum_pos[0] = regnum;
 *		    regnum_pos[1] = regsize;
 *		    csi_dcache_clean_invalid_range((uintptr_t)regnum_pos, regsize);
 *	    }
 *    }
 */
}

/*
 * Platform early initialization.
 */
static int spacemit_k1_early_init(bool cold_boot, const struct fdt_match *match)
{
	if (cold_boot) {
		/* initiate cci */
		cci_init(PLATFORM_CCI_ADDR, cci_map, array_size(cci_map));
		/* enable dcache */
		csi_enable_dcache();
		/* wakeup other core ? */
		wakeup_other_core();
		/* initialize */
#ifdef CONFIG_ARM_SCMI_PROTOCOL_SUPPORT
		plat_arm_pwrc_setup();
#endif
	} else {
#ifdef CONFIG_ARM_PSCI_SUPPORT
		psci_warmboot_entrypoint();
#endif
	}

	return 0;
}

#ifdef CONFIG_ARM_PSCI_SUPPORT
/** Start (or power-up) the given hart */
static int spacemit_hart_start(unsigned int hartid, unsigned long saddr)
{
	return psci_cpu_on_start(hartid, saddr);
}

/**
 * Stop (or power-down) the current hart from running. This call
 * doesn't expect to return if success.
 */
static int spacemit_hart_stop(void)
{
	psci_cpu_off();
	return 0;
}

static int spacemit_hart_suspend(unsigned int suspend_type)
{
	psci_cpu_suspend(suspend_type, 0, 0);
	return 0;
}

static void spacemit_hart_resume(void)
{
	psci_warmboot_entrypoint();
}

static const struct sbi_hsm_device spacemit_hsm_ops = {
	.name		= "spacemit-hsm",
	.hart_start	= spacemit_hart_start,
	.hart_stop	= spacemit_hart_stop,
	.hart_suspend	= spacemit_hart_suspend,
	.hart_resume	= spacemit_hart_resume,
};

static int spacemit_system_suspend_check(u32 sleep_type)
{
	return sleep_type == SBI_SUSP_SLEEP_TYPE_SUSPEND ? 0 : SBI_EINVAL;
}

static int spacemit_system_suspend(u32 sleep_type, unsigned long mmode_resume_addr)
{
	if (sleep_type != SBI_SUSP_SLEEP_TYPE_SUSPEND)
		return SBI_EINVAL;

	psci_system_suspend(mmode_resume_addr, 0);

	return SBI_OK;
}

static struct sbi_system_suspend_device spacemit_system_suspend_ops = {
	.name = "spacemit-system-suspend",
	.system_suspend_check = spacemit_system_suspend_check,
	.system_suspend = spacemit_system_suspend,
};
#endif

/*
 * Platform final initialization.
 */
static int spacemit_k1_final_init(bool cold_boot, const struct fdt_match *match)
{
#ifdef CONFIG_ARM_PSCI_SUPPORT
	/* for clod boot, we build the cpu topology structure */
	if (cold_boot) {
		sbi_hsm_set_device(&spacemit_hsm_ops);
		/* register system-suspend ops */
		sbi_system_suspend_set_device(&spacemit_system_suspend_ops);
		return psci_setup();
	}
#endif

	return 0;
}

static bool spacemit_cold_boot_allowed(u32 hartid, const struct fdt_match *match)
{
	/* enable core snoop */
	csr_set(CSR_ML2SETUP, 1 << (hartid % PLATFORM_MAX_CPUS_PER_CLUSTER));

	/* dealing with resuming process */
	if ((__sbi_hsm_hart_get_state(hartid) == SBI_HSM_STATE_SUSPENDED) && (hartid == 0))
		return false;

	return ((hartid == 0) ? true : false);
}

static const struct fdt_match spacemit_k1_match[] = {
	{ .compatible = "spacemit,k1-pro" },
	{ .compatible = "spacemit,k1x" },
	{ },
};

const struct platform_override spacemit_k1 = {
	.match_table = spacemit_k1_match,
	.early_init = spacemit_k1_early_init,
	.final_init = spacemit_k1_final_init,
	.cold_boot_allowed = spacemit_cold_boot_allowed,
};
