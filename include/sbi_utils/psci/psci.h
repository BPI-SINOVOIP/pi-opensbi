#ifndef __PSCI_H__
#define __PSCI_H__

#include <sbi/sbi_types.h>
#include <spacemit/spacemit_config.h>

#define MPIDR_AFFLVL0_VAL(mpidr) \
                (((mpidr) >> MPIDR_AFF0_SHIFT) & MPIDR_AFFINITY0_MASK)
#define MPIDR_AFFLVL1_VAL(mpidr) \
                (((mpidr) >> MPIDR_AFF1_SHIFT) & MPIDR_AFFINITY1_MASK)
/*
 *  Macros for local power states in ARM platforms encoded by State-ID field
 *  within the power-state parameter.
 */
/* Local power state for power domains in Run state. */
#define ARM_LOCAL_STATE_RUN     0U
/* Local power state for retention. Valid only for CPU power domains */
#define ARM_LOCAL_STATE_RET     1U
/* Local power state for OFF/power-down. Valid for CPU and cluster power
   domains */
#define ARM_LOCAL_STATE_OFF     2U

/*
 * This macro defines the deepest retention state possible. A higher state
 * id will represent an invalid or a power down state.
 */
#define PLAT_MAX_RET_STATE              ARM_LOCAL_STATE_RET

/*
 * This macro defines the deepest power down states possible. Any state ID
 * higher than this is invalid.
 */
#define PLAT_MAX_OFF_STATE              ARM_LOCAL_STATE_OFF

/*
 * Type for representing the local power state at a particular level.
 */
typedef unsigned char plat_local_state_t;

/* The local state macro used to represent RUN state. */
#define PSCI_LOCAL_STATE_RUN    0U

typedef unsigned long u_register_t;

/*******************************************************************************
 * PSCI error codes
 ******************************************************************************/
#define PSCI_E_SUCCESS          0
#define PSCI_E_NOT_SUPPORTED    -1
#define PSCI_E_INVALID_PARAMS   -2
#define PSCI_E_DENIED           -3
#define PSCI_E_ALREADY_ON       -4
#define PSCI_E_ON_PENDING       -5
#define PSCI_E_INTERN_FAIL      -6
#define PSCI_E_NOT_PRESENT      -7
#define PSCI_E_DISABLED         -8
#define PSCI_E_INVALID_ADDRESS  -9

#define PSCI_INVALID_MPIDR      ~((u_register_t)0)


/*
 * These are the states reported by the PSCI_AFFINITY_INFO API for the specified
 * CPU. The definitions of these states can be found in Section 5.7.1 in the
 * PSCI specification (ARM DEN 0022C).
 */
typedef enum {
	AFF_STATE_ON = 0U,
	AFF_STATE_OFF = 1U,
	AFF_STATE_ON_PENDING = 2U
} aff_info_state_t;

/*******************************************************************************
 * Structure used to store per-cpu information relevant to the PSCI service.
 * It is populated in the per-cpu data array. In return we get a guarantee that
 * this information will not reside on a cache line shared with another cpu.
 ******************************************************************************/
typedef struct psci_cpu_data {
	/* State as seen by PSCI Affinity Info API */
	aff_info_state_t aff_info_state;

	/*
	 * Highest power level which takes part in a power management
	 * operation.
	 */
	unsigned int target_pwrlvl;

	/* The local power state of this CPU */
	plat_local_state_t local_state;
} psci_cpu_data_t;

/*
 * Macro to represent invalid affinity level within PSCI.
 */
#define PSCI_INVALID_PWR_LVL    (PLAT_MAX_PWR_LVL + 1U)

/*
 * These are the power states reported by PSCI_NODE_HW_STATE API for the
 * specified CPU. The definitions of these states can be found in Section 5.15.3
 * of PSCI specification (ARM DEN 0022C).
 */
#define HW_ON           0
#define HW_OFF          1
#define HW_STANDBY      2

#define PSTATE_ID_SHIFT         (0U)
#define PSTATE_VALID_MASK       (0xFCFE0000U)
#define PSTATE_TYPE_SHIFT       (16U)
#define PSTATE_PWR_LVL_SHIFT    (24U)
#define PSTATE_ID_MASK          (0xffffU)
#define PSTATE_PWR_LVL_MASK     (0x3U)

#define psci_get_pstate_pwrlvl(pstate)  (((pstate) >> PSTATE_PWR_LVL_SHIFT) & \
                                        PSTATE_PWR_LVL_MASK)
#define psci_make_powerstate(state_id, type, pwrlvl) \
                        (((state_id) & PSTATE_ID_MASK) << PSTATE_ID_SHIFT) |\
                        (((type) & PSTATE_TYPE_MASK) << PSTATE_TYPE_SHIFT) |\
                        (((pwrlvl) & PSTATE_PWR_LVL_MASK) << PSTATE_PWR_LVL_SHIFT)

#define PSTATE_TYPE_STANDBY     (0x0U)
#define PSTATE_TYPE_POWERDOWN   (0x1U)
#define PSTATE_TYPE_MASK        (0x1U)

/* RISCV suspend power state */
#define RSTATE_TYPE_SHIFT	(31U)
#define RSTATE_PWR_LVL_SHIFT	(24U)
#define RSTATE_COMMON_SHIFT	(28U)

/*****************************************************************************
 * This data structure defines the representation of the power state parameter
 * for its exchange between the generic PSCI code and the platform port. For
 * example, it is used by the platform port to specify the requested power
 * states during a power management operation. It is used by the generic code to
 * inform the platform about the target power states that each level should
 * enter.
 ****************************************************************************/
typedef struct psci_power_state {
        /*
         * The pwr_domain_state[] stores the local power state at each level
         * for the CPU.
         */
        plat_local_state_t pwr_domain_state[PLAT_MAX_PWR_LVL + 1U ];
} psci_power_state_t;

/*
 * Function to test whether the plat_local_state is RUN state
 */
static inline int is_local_state_run(unsigned int plat_local_state)
{
        return (plat_local_state == PSCI_LOCAL_STATE_RUN) ? 1 : 0;
}

/*
 * Function to test whether the plat_local_state is OFF state
 */
static inline int is_local_state_off(unsigned int plat_local_state)
{
        return ((plat_local_state > PLAT_MAX_RET_STATE) &&
                (plat_local_state <= PLAT_MAX_OFF_STATE)) ? 1 : 0;
}

/* Power state helper functions */

static inline unsigned int psci_check_power_state(unsigned int power_state)
{
        return ((power_state) & PSTATE_VALID_MASK);
}

static inline unsigned int psci_get_pstate_id(unsigned int power_state)
{
        return ((power_state) >> PSTATE_ID_SHIFT) & PSTATE_ID_MASK;
}

static inline unsigned int psci_get_pstate_type(unsigned int power_state)
{
        return ((power_state) >> PSTATE_TYPE_SHIFT) & PSTATE_TYPE_MASK;
}

/*******************************************************************************
 * Structure populated by platform specific code to export routines which
 * perform common low level power management functions
 ******************************************************************************/
typedef struct plat_psci_ops {
        void (*cpu_standby)(plat_local_state_t cpu_state);
        int (*pwr_domain_on)(u_register_t mpidr);
        void (*pwr_domain_off)(const psci_power_state_t *target_state);
        int (*pwr_domain_off_early)(const psci_power_state_t *target_state);
        void (*pwr_domain_suspend_pwrdown_early)(
                                const psci_power_state_t *target_state);
        void (*pwr_domain_suspend)(const psci_power_state_t *target_state);
        void (*pwr_domain_on_finish)(const psci_power_state_t *target_state);
        void (*pwr_domain_on_finish_late)(
                                const psci_power_state_t *target_state);
        void (*pwr_domain_suspend_finish)(
                                const psci_power_state_t *target_state);
        void (*pwr_domain_pwr_down_wfi)(
                                const psci_power_state_t *target_state);
        void (*system_off)(void);
        void (*system_reset)(void);
        int (*validate_power_state)(unsigned int power_state,
                                    psci_power_state_t *req_state);
        int (*validate_ns_entrypoint)(uintptr_t ns_entrypoint);
        void (*get_sys_suspend_power_state)(
                                    psci_power_state_t *req_state);
        int (*get_pwr_lvl_state_idx)(plat_local_state_t pwr_domain_state,
                                    int pwrlvl);
        int (*translate_power_state_by_mpidr)(u_register_t mpidr,
                                    unsigned int power_state,
                                    psci_power_state_t *output_state);
        int (*get_node_hw_state)(u_register_t mpidr, unsigned int power_level);
        int (*mem_protect_chk)(uintptr_t base, u_register_t length);
        int (*read_mem_protect)(int *val);
        int (*write_mem_protect)(int val);
        int (*system_reset2)(int is_vendor,
                                int reset_type, u_register_t cookie);
} plat_psci_ops_t;

int psci_cpu_on(u_register_t target_cpu, uintptr_t entrypoint);
int psci_cpu_off(void);
int psci_affinity_info(u_register_t target_affinity, unsigned int lowest_affinity_level);
int psci_cpu_suspend(unsigned int power_state, uintptr_t entrypoint, u_register_t context_id);

#endif
