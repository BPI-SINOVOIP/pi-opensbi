#ifndef __DRIVER_SCMI_H__
#define __DRIVER_SCMI_H__

#include <sbi_utils/psci/psci.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_types.h>

#define GET_SCMI_MAJOR_VER(ver)                 (((ver) >> 16) & 0xffff)
#define GET_SCMI_MINOR_VER(ver)                 ((ver) & 0xffff)

#define MAKE_SCMI_VERSION(maj, min)     \
                        ((((maj) & 0xffff) << 16) | ((min) & 0xffff))

/* Supported SCMI Protocol Versions */
#define SCMI_AP_CORE_PROTO_VER                  MAKE_SCMI_VERSION(1, 0)
#define SCMI_PWR_DMN_PROTO_VER                  MAKE_SCMI_VERSION(2, 0)
#define SCMI_SYS_PWR_PROTO_VER                  MAKE_SCMI_VERSION(1, 0)

/*
 * Check that the driver's version is same or higher than the reported SCMI
 * version. We accept lower major version numbers, as all affected protocols
 * so far stay backwards compatible. This might need to be revisited in the
 * future.
 */
#define is_scmi_version_compatible(drv, scmi)                           \
        ((GET_SCMI_MAJOR_VER(drv) > GET_SCMI_MAJOR_VER(scmi)) ||        \
        ((GET_SCMI_MAJOR_VER(drv) == GET_SCMI_MAJOR_VER(scmi)) &&       \
        (GET_SCMI_MINOR_VER(drv) <= GET_SCMI_MINOR_VER(scmi))))

/* Mandatory messages IDs for all SCMI protocols */
#define SCMI_PROTO_VERSION_MSG                  0x0
#define SCMI_PROTO_ATTR_MSG                     0x1
#define SCMI_PROTO_MSG_ATTR_MSG                 0x2

/* SCMI power domain management protocol message IDs */
#define SCMI_PWR_STATE_SET_MSG                  0x4
#define SCMI_PWR_STATE_GET_MSG                  0x5

/* SCMI system power management protocol message IDs */
#define SCMI_SYS_PWR_STATE_SET_MSG              0x3
#define SCMI_SYS_PWR_STATE_GET_MSG              0x4

/* SCMI Protocol identifiers */
#define SCMI_PWR_DMN_PROTO_ID                   0x11
#define SCMI_SYS_PWR_PROTO_ID                   0x12

/*
 * Macros to describe the bit-fields of the `attribute` of system power domain
 * protocol PROTOCOL_MSG_ATTRIBUTE message.
 */
#define SYS_PWR_ATTR_WARM_RESET_SHIFT           31
#define SCMI_SYS_PWR_WARM_RESET_SUPPORTED       (1U << SYS_PWR_ATTR_WARM_RESET_SHIFT)

#define SYS_PWR_ATTR_SUSPEND_SHIFT              30
#define SCMI_SYS_PWR_SUSPEND_SUPPORTED          (1 << SYS_PWR_ATTR_SUSPEND_SHIFT)

/*
 * Macros to describe the bit-fields of the `flags` parameter of system power
 * domain protocol SYSTEM_POWER_STATE_SET message.
 */
#define SYS_PWR_SET_GRACEFUL_REQ_SHIFT          0
#define SCMI_SYS_PWR_GRACEFUL_REQ               (1 << SYS_PWR_SET_GRACEFUL_REQ_SHIFT)
#define SCMI_SYS_PWR_FORCEFUL_REQ               (0 << SYS_PWR_SET_GRACEFUL_REQ_SHIFT)

/*
 * Macros to describe the `system_state` parameter of system power
 * domain protocol SYSTEM_POWER_STATE_SET message.
 */
#define SCMI_SYS_PWR_SHUTDOWN                   0x0
#define SCMI_SYS_PWR_COLD_RESET                 0x1
#define SCMI_SYS_PWR_WARM_RESET                 0x2
#define SCMI_SYS_PWR_POWER_UP                   0x3
#define SCMI_SYS_PWR_SUSPEND                    0x4

/* SCMI Error code definitions */
#define SCMI_E_QUEUED                   1
#define SCMI_E_SUCCESS                  0
#define SCMI_E_NOT_SUPPORTED            -1
#define SCMI_E_INVALID_PARAM            -2
#define SCMI_E_DENIED                   -3
#define SCMI_E_NOT_FOUND                -4
#define SCMI_E_OUT_OF_RANGE             -5
#define SCMI_E_BUSY                     -6

/*
 * SCMI driver platform information. The details of the doorbell mechanism
 * can be found in the SCMI specification.
 */
typedef struct scmi_channel_plat_info {
        /* SCMI mailbox memory */
        uintptr_t scmi_mbx_mem;
        /* The door bell register address */
        uintptr_t db_reg_addr;
        /* The bit mask that need to be preserved when ringing doorbell */
        uint32_t db_preserve_mask;
        /* The bit mask that need to be set to ring doorbell */
        uint32_t db_modify_mask;
        /* The handler for ringing doorbell */
        void (*ring_doorbell)(struct scmi_channel_plat_info *plat_info);
        /* cookie is unused now. But added for future enhancements. */
        void *cookie;
} scmi_channel_plat_info_t;

typedef spinlock_t scmi_lock_t;

/*
 * Structure to represent an SCMI channel.
 */
typedef struct scmi_channel {
        scmi_channel_plat_info_t *info;
         /* The lock for channel access */
        scmi_lock_t *lock;
        /* Indicate whether the channel is initialized */
        int is_initialized;
} scmi_channel_t;

/* External Common API */
void *scmi_init(scmi_channel_t *ch);
/* API to override default PSCI callbacks for platforms that support SCMI. */
const plat_psci_ops_t *css_scmi_override_pm_ops(plat_psci_ops_t *ops);

/*
 * Power domain protocol commands. Refer to the SCMI specification for more
 * details on these commands.
 */
int scmi_pwr_state_set(void *p, uint32_t domain_id, uint32_t scmi_pwr_state);
int scmi_pwr_state_get(void *p, uint32_t domain_id, uint32_t *scmi_pwr_state);

int scmi_proto_version(void *p, uint32_t proto_id, uint32_t *version);
int scmi_proto_msg_attr(void *p, uint32_t proto_id, uint32_t command_id,
                                                uint32_t *attr);
scmi_channel_plat_info_t *plat_css_get_scmi_info(unsigned int channel_id);

/*
 * System power management protocol commands. Refer SCMI specification for more
 * details on these commands.
 */
int scmi_sys_pwr_state_set(void *p, uint32_t flags, uint32_t system_state);
int scmi_sys_pwr_state_get(void *p, uint32_t *system_state);

#endif
