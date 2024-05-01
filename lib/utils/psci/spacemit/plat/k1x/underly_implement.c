#include <sbi/riscv_io.h>
#include <sbi/sbi_types.h>
#include <sbi/riscv_asm.h>
#include <sbi_utils/psci/psci.h>
#include <sbi/sbi_console.h>
#include <spacemit/spacemit_config.h>

struct pmu_cap_wakeup {
	unsigned int pmu_cap_core0_wakeup;
	unsigned int pmu_cap_core1_wakeup;
	unsigned int pmu_cap_core2_wakeup;
	unsigned int pmu_cap_core3_wakeup;
};

/* D1P & D2 ? */
void spacemit_top_on(u_register_t mpidr)
{
	unsigned int *cluster0_acpr = NULL;
	unsigned int *cluster1_acpr = NULL;

	cluster0_acpr = (unsigned int *)PMU_ACPR_CLUSTER0_REG;
	cluster1_acpr = (unsigned int *)PMU_ACPR_CLUSTER1_REG;

	unsigned int value = readl(cluster0_acpr);
	value &= ~((1 << CLUSTER_AXISDO_OFFSET) |
		(1 << CLUSTER_DDRSD_OFFSET) |
		(1 << CLUSTER_APBSD_OFFSET) |
		(1 << CLUSTER_VCXOSD_OFFSET) |
		(1 << CLUSTER_BIT29_OFFSET) |
		(1 << CLUSTER_BIT14_OFFSET) |
		(1 << CLUSTER_BIT30_OFFSET) |
		(1 << CLUSTER_BIT25_OFFSET) |
		(1 << CLUSTER_BIT13_OFFSET));
	writel(value, cluster0_acpr);

	value = readl(cluster1_acpr);
	value &= ~((1 << CLUSTER_AXISDO_OFFSET) |
		(1 << CLUSTER_DDRSD_OFFSET) |
		(1 << CLUSTER_APBSD_OFFSET) |
		(1 << CLUSTER_VCXOSD_OFFSET) |
		(1 << CLUSTER_BIT29_OFFSET) |
		(1 << CLUSTER_BIT14_OFFSET) |
		(1 << CLUSTER_BIT30_OFFSET) |
		(1 << CLUSTER_BIT25_OFFSET) |
		(1 << CLUSTER_BIT13_OFFSET));
	writel(value, cluster1_acpr);
}

/* D1P & D2 ? */
void spacemit_top_off(u_register_t mpidr)
{
	unsigned int *cluster0_acpr = NULL;
	unsigned int *cluster1_acpr = NULL;

	cluster0_acpr = (unsigned int *)PMU_ACPR_CLUSTER0_REG;
	cluster1_acpr = (unsigned int *)PMU_ACPR_CLUSTER1_REG;

	unsigned int value = readl(cluster0_acpr);
	value |= (1 << CLUSTER_AXISDO_OFFSET) |
		(1 << CLUSTER_DDRSD_OFFSET) |
		(1 << CLUSTER_APBSD_OFFSET) |
		(1 << CLUSTER_VCXOSD_OFFSET) |
		(1 << CLUSTER_BIT29_OFFSET) |
		(1 << CLUSTER_BIT14_OFFSET) |
		(1 << CLUSTER_BIT30_OFFSET) |
		(1 << CLUSTER_BIT25_OFFSET) |
		(1 << CLUSTER_BIT13_OFFSET);
	writel(value, cluster0_acpr);

	value = readl(cluster1_acpr);
	value |= (1 << CLUSTER_AXISDO_OFFSET) |
		(1 << CLUSTER_DDRSD_OFFSET) |
		(1 << CLUSTER_APBSD_OFFSET) |
		(1 << CLUSTER_VCXOSD_OFFSET) |
		(1 << CLUSTER_BIT29_OFFSET) |
		(1 << CLUSTER_BIT14_OFFSET) |
		(1 << CLUSTER_BIT30_OFFSET) |
		(1 << CLUSTER_BIT25_OFFSET) |
		(1 << CLUSTER_BIT13_OFFSET);
	writel(value, cluster1_acpr);

	value = readl((unsigned int *)PMU_ACPR_UNKONW_REG);
	value |= (1 << 2);
	writel(value, (unsigned int *)PMU_ACPR_UNKONW_REG);

	/* for wakeup debug */
	writel(0xffff, (unsigned int *)0xd4051030);
}

/* M2 */
void spacemit_cluster_on(u_register_t mpidr)
{
	unsigned int target_cpu_idx, value;
	unsigned int *cluster_assert_base0 = NULL;
	unsigned int *cluster_assert_base1 = NULL;
	unsigned int *cluster_assert_base2 = NULL;
	unsigned int *cluster_assert_base3 = NULL;
	unsigned int *cluster_assert_base4 = NULL;
	unsigned int *cluster_assert_base5 = NULL;
	unsigned int *cluster_assert_base6 = NULL;
	unsigned int *cluster_assert_base7 = NULL;

	target_cpu_idx = MPIDR_AFFLVL1_VAL(mpidr) * PLATFORM_MAX_CPUS_PER_CLUSTER
			+ MPIDR_AFFLVL0_VAL(mpidr);

	switch (target_cpu_idx) {
		case 0:
		case 1:
		case 2:
		case 3:
			cluster_assert_base0 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG0;
			cluster_assert_base1 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG1;
			cluster_assert_base2 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG2;
			cluster_assert_base3 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG3;

			/* cluster vote */
			/* M2 */
			value = readl(cluster_assert_base0);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base0);

			value = readl(cluster_assert_base1);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base1);

			value = readl(cluster_assert_base2);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base2);

			value = readl(cluster_assert_base3);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base3);
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			cluster_assert_base4 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG0;
			cluster_assert_base5 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG1;
			cluster_assert_base6 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG2;
			cluster_assert_base7 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG3;
			
			/* cluster vote */
			/* M2 */
			value = readl(cluster_assert_base4);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base4);

			value = readl(cluster_assert_base5);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base5);

			value = readl(cluster_assert_base6);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base6);

			value = readl(cluster_assert_base7);
			value &= ~CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base7);
			break;
	}
}

/* M2 */
void spacemit_cluster_off(u_register_t mpidr)
{
	unsigned int target_cpu_idx, value;
	unsigned int *cluster_assert_base0 = NULL;
	unsigned int *cluster_assert_base1 = NULL;
	unsigned int *cluster_assert_base2 = NULL;
	unsigned int *cluster_assert_base3 = NULL;
	unsigned int *cluster_assert_base4 = NULL;
	unsigned int *cluster_assert_base5 = NULL;
	unsigned int *cluster_assert_base6 = NULL;
	unsigned int *cluster_assert_base7 = NULL;

	target_cpu_idx = MPIDR_AFFLVL1_VAL(mpidr) * PLATFORM_MAX_CPUS_PER_CLUSTER
			+ MPIDR_AFFLVL0_VAL(mpidr);

	switch (target_cpu_idx) {
		case 0:
		case 1:
		case 2:
		case 3:
			cluster_assert_base0 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG0;
			cluster_assert_base1 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG1;
			cluster_assert_base2 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG2;
			cluster_assert_base3 = (unsigned int *)PMU_C0_CAPMP_IDLE_CFG3;

			/* cluster vote */
			/* M2 */
			value = readl(cluster_assert_base0);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base0);

			value = readl(cluster_assert_base1);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base1);

			value = readl(cluster_assert_base2);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base2);

			value = readl(cluster_assert_base3);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base3);
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			cluster_assert_base4 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG0;
			cluster_assert_base5 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG1;
			cluster_assert_base6 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG2;
			cluster_assert_base7 = (unsigned int *)PMU_C1_CAPMP_IDLE_CFG3;
			
			/* cluster vote */
			/* M2 */
			value = readl(cluster_assert_base4);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base4);

			value = readl(cluster_assert_base5);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base5);

			value = readl(cluster_assert_base6);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base6);

			value = readl(cluster_assert_base7);
			value |= CLUSTER_PWR_DOWN_VALUE;
			writel(value, cluster_assert_base7);
			break;
	}
}

void spacemit_wakeup_cpu(u_register_t mpidr)
{
	unsigned int *cpu_reset_base;
	struct pmu_cap_wakeup *pmu_cap_wakeup;
	unsigned int cur_cluster, cur_cpu;
	unsigned int target_cpu_idx;
	unsigned int cur_hartid = current_hartid();

	cur_cluster = MPIDR_AFFLVL1_VAL(cur_hartid);
	cur_cpu = MPIDR_AFFLVL0_VAL(cur_hartid);

	pmu_cap_wakeup = (struct pmu_cap_wakeup *)((cur_cluster == 0) ? (unsigned int *)CPU_RESET_BASE_ADDR :
			(unsigned int *)C1_CPU_RESET_BASE_ADDR);

	switch (cur_cpu) {
		case 0:
			cpu_reset_base = &pmu_cap_wakeup->pmu_cap_core0_wakeup;
			break;
		case 1:
			cpu_reset_base = &pmu_cap_wakeup->pmu_cap_core1_wakeup;
			break;
		case 2:
			cpu_reset_base = &pmu_cap_wakeup->pmu_cap_core2_wakeup;
			break;
		case 3:
			cpu_reset_base = &pmu_cap_wakeup->pmu_cap_core3_wakeup;
			break;
	}

	target_cpu_idx = MPIDR_AFFLVL1_VAL(mpidr) * PLATFORM_MAX_CPUS_PER_CLUSTER
			+ MPIDR_AFFLVL0_VAL(mpidr);

	writel(1 << target_cpu_idx, cpu_reset_base);
}

void spacemit_assert_cpu(u_register_t mpidr)
{
	unsigned int target_cpu_idx;
	unsigned int *cpu_assert_base = NULL;

	target_cpu_idx = MPIDR_AFFLVL1_VAL(mpidr) * PLATFORM_MAX_CPUS_PER_CLUSTER
			+ MPIDR_AFFLVL0_VAL(mpidr);

	switch (target_cpu_idx) {
		case 0:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE0_IDLE_CFG;
			break;
		case 1:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE1_IDLE_CFG;
			break;
		case 2:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE2_IDLE_CFG;
			break;
		case 3:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE3_IDLE_CFG;
			break;
		case 4:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE4_IDLE_CFG;
			break;
		case 5:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE5_IDLE_CFG;
			break;
		case 6:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE6_IDLE_CFG;
			break;
		case 7:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE7_IDLE_CFG;
			break;
	}

	/* cpu vote */
	/* C2 */
	unsigned int value = readl(cpu_assert_base);
	value |= CPU_PWR_DOWN_VALUE;
	writel(value, cpu_assert_base);
}

void spacemit_deassert_cpu(void)
{
	unsigned int mpidr = current_hartid();

	/* clear the idle bit */
	unsigned int target_cpu_idx;
	unsigned int *cpu_assert_base = NULL;

	target_cpu_idx = MPIDR_AFFLVL1_VAL(mpidr) * PLATFORM_MAX_CPUS_PER_CLUSTER
			+ MPIDR_AFFLVL0_VAL(mpidr);

	switch (target_cpu_idx) {
		case 0:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE0_IDLE_CFG;
			break;
		case 1:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE1_IDLE_CFG;
			break;
		case 2:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE2_IDLE_CFG;
			break;
		case 3:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE3_IDLE_CFG;
			break;
		case 4:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE4_IDLE_CFG;
			break;
		case 5:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE5_IDLE_CFG;
			break;
		case 6:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE6_IDLE_CFG;
			break;
		case 7:
			cpu_assert_base = (unsigned int *)PMU_CAP_CORE7_IDLE_CFG;
			break;
	}

	/* de-vote cpu */
	unsigned int value = readl(cpu_assert_base);
	value &= ~CPU_PWR_DOWN_VALUE;
	writel(value, cpu_assert_base);
}
