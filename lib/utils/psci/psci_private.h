#ifndef __PSCI_PRIVATE_H__
#define __PSCI_PRIVATE_H__

#include <sbi/riscv_locks.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/psci/psci.h>
#include <sbi_utils/cache/cacheflush.h>

/*******************************************************************************
 * The following two data structures implement the power domain tree. The tree
 * is used to track the state of all the nodes i.e. power domain instances
 * described by the platform. The tree consists of nodes that describe CPU power
 * domains i.e. leaf nodes and all other power domains which are parents of a
 * CPU power domain i.e. non-leaf nodes.
 ******************************************************************************/
typedef struct non_cpu_pwr_domain_node {
	/*
	 * Index of the first CPU power domain node level 0 which has this node
	 * as its parent.
	 */
	unsigned int cpu_start_idx;

	/*
	 * Number of CPU power domains which are siblings of the domain indexed
	 * by 'cpu_start_idx' i.e. all the domains in the range 'cpu_start_idx
	 * -> cpu_start_idx + ncpus' have this node as their parent.
	 */
	unsigned int ncpus;

	/*
	 * Index of the parent power domain node.
	 * TODO: Figure out whether to whether using pointer is more efficient.
	 */
	unsigned int parent_node;

	plat_local_state_t local_state;

	unsigned char level;

	/* For indexing the psci_lock array*/
	unsigned short lock_index;
} __aligned(CACHE_LINE_SIZE) non_cpu_pd_node_t;

typedef struct cpu_pwr_domain_node {
	u_register_t mpidr;

	/*
	 * Index of the parent power domain node.
	 * TODO: Figure out whether to whether using pointer is more efficient.
	 */
	unsigned int parent_node;

	/*
	 * A CPU power domain does not require state coordination like its
	 * parent power domains. Hence this node does not include a bakery
	 * lock. A spinlock is required by the CPU_ON handler to prevent a race
	 * when multiple CPUs try to turn ON the same target CPU.
	 */
	spinlock_t cpu_lock;
} cpu_pd_node_t;

/*
 * On systems where participant CPUs are cache-coherent, we can use spinlocks
 * instead of bakery locks.
 */
typedef struct psci_spinlock_t {
	spinlock_t lock;
} __aligned(CACHE_LINE_SIZE) _psci_spinlock_t;

#define DEFINE_PSCI_LOCK(_name)         _psci_spinlock_t _name
#define DECLARE_PSCI_LOCK(_name)        extern DEFINE_PSCI_LOCK(_name)

/* One lock is required per non-CPU power domain node */
DECLARE_PSCI_LOCK(psci_locks[PSCI_NUM_NON_CPU_PWR_DOMAINS]);

static inline void psci_lock_init(non_cpu_pd_node_t *non_cpu_pd_node, unsigned short idx)
{
        non_cpu_pd_node[idx].lock_index = idx;
}

static inline void psci_lock_get(non_cpu_pd_node_t *non_cpu_pd_node)
{
        spin_lock(&psci_locks[non_cpu_pd_node->lock_index].lock);
}

static inline void psci_lock_release(non_cpu_pd_node_t *non_cpu_pd_node)
{
        spin_unlock(&psci_locks[non_cpu_pd_node->lock_index].lock);
}

/* common */
extern non_cpu_pd_node_t psci_non_cpu_pd_nodes[PSCI_NUM_NON_CPU_PWR_DOMAINS];
extern cpu_pd_node_t psci_cpu_pd_nodes[PLATFORM_CORE_COUNT];
extern unsigned int psci_plat_core_count;
extern unsigned long psci_delta_off;
extern const plat_psci_ops_t *psci_plat_pm_ops;

void psci_acquire_pwr_domain_locks(unsigned int end_pwrlvl,
                                   const unsigned int *parent_nodes);
void psci_release_pwr_domain_locks(unsigned int end_pwrlvl,
                                   const unsigned int *parent_nodes);
unsigned int psci_find_max_off_lvl(const psci_power_state_t *state_info);

int psci_validate_mpidr(u_register_t mpidr);
void psci_get_parent_pwr_domain_nodes(unsigned int cpu_idx,
				      unsigned int end_lvl,
				      unsigned int *node_index);

void psci_init_req_local_pwr_states(void);
void set_non_cpu_pd_node_local_state(unsigned int parent_idx,
		plat_local_state_t state);
void psci_set_req_local_pwr_state(unsigned int pwrlvl,
					 unsigned int cpu_idx,
					 plat_local_state_t req_pwr_state);
void psci_set_aff_info_state(aff_info_state_t aff_state);
aff_info_state_t psci_get_aff_info_state(void);
aff_info_state_t psci_get_aff_info_state_by_idx(unsigned int idx);
void psci_set_aff_info_state_by_idx(unsigned int idx, aff_info_state_t aff_state);
void psci_set_cpu_local_state(plat_local_state_t state);
void psci_set_pwr_domains_to_run(unsigned int end_pwrlvl);

void psci_get_target_local_pwr_states(unsigned int end_pwrlvl,
                                      psci_power_state_t *target_state);

void psci_do_state_coordination(unsigned int end_pwrlvl,
                                psci_power_state_t *state_info);

int plat_core_pos_by_mpidr(u_register_t mpidr);
int psci_validate_power_state(unsigned int power_state,
                              psci_power_state_t *state_info);
int psci_validate_suspend_req(const psci_power_state_t *state_info,
                              unsigned int is_power_down_state);
unsigned int psci_find_max_off_lvl(const psci_power_state_t *state_info);
unsigned int psci_find_target_suspend_lvl(const psci_power_state_t *state_info);

void psci_set_suspend_pwrlvl(unsigned int target_lvl);
/* Private exported functions from psci_suspend.c */
int psci_cpu_suspend_start(/* const entry_point_info_t *ep */ uintptr_t entrypoint,
                           unsigned int end_pwrlvl,
                           psci_power_state_t *state_info,
                           unsigned int is_power_down_state);
void psci_cpu_suspend_finish(unsigned int cpu_idx, const psci_power_state_t *state_info);
void riscv_pwr_state_to_psci(unsigned int rstate, unsigned int *pstate);

bool psci_is_last_on_cpu(void);
void psci_query_sys_suspend_pwrstate(psci_power_state_t *state_info);
int psci_system_suspend(uintptr_t entrypoint, u_register_t context_id);

/* Helper function to identify a CPU standby request in PSCI Suspend call */
static inline bool is_cpu_standby_req(unsigned int is_power_down_state,
                                      unsigned int retn_lvl)
{
        return (is_power_down_state == 0U) && (retn_lvl == 0U);
}

static inline void psci_do_pwrup_cache_maintenance(uintptr_t scratch)
{
	/* invalidate local cache */
	csi_invalidate_dcache_all();

	/* enable dcache */
	csi_enable_dcache();
}

static inline void psci_disable_core_snoop(void)
{
	unsigned int hartid = current_hartid();

	csr_clear(CSR_ML2SETUP, 1 << (hartid % PLATFORM_MAX_CPUS_PER_CLUSTER));
}

static inline void psci_do_pwrdown_cache_maintenance(int hartid, uintptr_t scratch, int power_level)
{
	/* disable the data preftch */
	csi_disable_data_preftch();

	/* flush dacache all */
	csi_flush_dcache_all();

	if (power_level >= PSCI_CPU_PWR_LVL + 1) {
#if defined(CONFIG_PLATFORM_SPACEMIT_K1X)
		/* disable the tcm */
		csr_write(CSR_TCMCFG, 0);
#endif
		csi_flush_l2_cache(0);
	}

	/* disable dcache */
	csi_disable_dcache();

	/* disable core snoop */
	psci_disable_core_snoop();

	asm volatile ("fence iorw, iorw");
}

/* psci cpu */
int psci_cpu_on_start(u_register_t target, uintptr_t entrypoint);
void psci_cpu_on_finish(unsigned int cpu_idx, const psci_power_state_t *state_info);
int psci_do_cpu_off(unsigned int end_pwrlvl);

#endif
