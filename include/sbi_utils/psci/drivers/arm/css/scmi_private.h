#ifndef __SCMI_PRIVATE_H__
#define __SCMI_PRIVATE_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include <sbi_utils/psci/drivers/arm/css/scmi.h>

/*
 * SCMI power domain management protocol message and response lengths. It is
 * calculated as sum of length in bytes of the message header (4) and payload
 * area (the number of bytes of parameters or return values in the payload).
 */
#define SCMI_PROTO_VERSION_MSG_LEN              4
#define SCMI_PROTO_VERSION_RESP_LEN             12

#define SCMI_PROTO_MSG_ATTR_MSG_LEN             8
#define SCMI_PROTO_MSG_ATTR_RESP_LEN            12

#define SCMI_PWR_STATE_GET_MSG_LEN              8
#define SCMI_PWR_STATE_GET_RESP_LEN             12

/* SCMI power domain protocol `POWER_STATE_SET` message flags */
#define SCMI_PWR_STATE_SET_FLAG_SYNC    0
#define SCMI_PWR_STATE_SET_FLAG_ASYNC   1

/* SCMI message header format bit field */
#define SCMI_MSG_ID_SHIFT               0
#define SCMI_MSG_ID_WIDTH               8
#define SCMI_MSG_ID_MASK                ((1 << SCMI_MSG_ID_WIDTH) - 1)

#define SCMI_MSG_PROTO_ID_SHIFT         10
#define SCMI_MSG_PROTO_ID_WIDTH         8
#define SCMI_MSG_PROTO_ID_MASK          ((1 << SCMI_MSG_PROTO_ID_WIDTH) - 1)

#define SCMI_MSG_TOKEN_SHIFT            18
#define SCMI_MSG_TOKEN_WIDTH            10
#define SCMI_MSG_TOKEN_MASK             ((1 << SCMI_MSG_TOKEN_WIDTH) - 1)

#define SCMI_PWR_STATE_SET_MSG_LEN              16
#define SCMI_PWR_STATE_SET_RESP_LEN             8

#define SCMI_SYS_PWR_STATE_SET_MSG_LEN          12
#define SCMI_SYS_PWR_STATE_SET_RESP_LEN         8

#define SCMI_SYS_PWR_STATE_GET_MSG_LEN          4
#define SCMI_SYS_PWR_STATE_GET_RESP_LEN         12

/* SCMI mailbox flags */
#define SCMI_FLAG_RESP_POLL     0
#define SCMI_FLAG_RESP_INT      1

/* Helper macros to copy arguments to the mailbox payload */
#define SCMI_PAYLOAD_ARG1(payld_arr, arg1)                              \
                *((uint32_t *)&payld_arr[0]) = arg1

#define SCMI_PAYLOAD_ARG2(payld_arr, arg1, arg2)        do {            \
                SCMI_PAYLOAD_ARG1(payld_arr, arg1);                     \
                *((uint32_t *)&payld_arr[1]) = arg2;          		\
        } while (0)

#define SCMI_PAYLOAD_ARG3(payld_arr, arg1, arg2, arg3)  do {            \
                SCMI_PAYLOAD_ARG2(payld_arr, arg1, arg2);               \
                *((uint32_t *)&payld_arr[2]) = arg3;          \
        } while (0)

/* Helper macros to read return values from the mailbox payload */
#define SCMI_PAYLOAD_RET_VAL1(payld_arr, val1)                          \
                (val1) = *((uint32_t *)&payld_arr[0])

#define SCMI_PAYLOAD_RET_VAL2(payld_arr, val1, val2)    do {            \
                SCMI_PAYLOAD_RET_VAL1(payld_arr, val1);                 \
                (val2) = *((uint32_t *)&payld_arr[1]);        \
        } while (0)

#define SCMI_PAYLOAD_RET_VAL3(payld_arr, val1, val2, val3)      do {    \
                SCMI_PAYLOAD_RET_VAL2(payld_arr, val1, val2);           \
                (val3) = *((uint32_t *)&payld_arr[2]);        \
        } while (0)

#define SCMI_PAYLOAD_RET_VAL4(payld_arr, val1, val2, val3, val4)        do {    \
                SCMI_PAYLOAD_RET_VAL3(payld_arr, val1, val2, val3);             \
                (val4) = *((uint32_t *)&payld_arr[3]);                \
        } while (0)

/* Helper macro to get the token from a SCMI message header */
#define SCMI_MSG_GET_TOKEN(_msg)                                \
        (((_msg) >> SCMI_MSG_TOKEN_SHIFT) & SCMI_MSG_TOKEN_MASK)

/* SCMI Channel Status bit fields */
#define SCMI_CH_STATUS_RES0_MASK        0xFFFFFFFE
#define SCMI_CH_STATUS_FREE_SHIFT       0
#define SCMI_CH_STATUS_FREE_WIDTH       1
#define SCMI_CH_STATUS_FREE_MASK        ((1 << SCMI_CH_STATUS_FREE_WIDTH) - 1)

/* Helper macros to check and write the channel status */
#define SCMI_IS_CHANNEL_FREE(status)                                    \
        (!!(((status) >> SCMI_CH_STATUS_FREE_SHIFT) & SCMI_CH_STATUS_FREE_MASK))

#define SCMI_MARK_CHANNEL_BUSY(status)  do {                            \
                if (!SCMI_IS_CHANNEL_FREE(status))                   \
			sbi_hart_hang();				\
                (status) &= ~(SCMI_CH_STATUS_FREE_MASK <<               \
                                SCMI_CH_STATUS_FREE_SHIFT);             \
        } while (0)

/*
 * Helper macro to create an SCMI message header given protocol, message id
 * and token.
 */
#define SCMI_MSG_CREATE(_protocol, _msg_id, _token)                             \
        ((((_protocol) & SCMI_MSG_PROTO_ID_MASK) << SCMI_MSG_PROTO_ID_SHIFT) |  \
        (((_msg_id) & SCMI_MSG_ID_MASK) << SCMI_MSG_ID_SHIFT) |                 \
        (((_token) & SCMI_MSG_TOKEN_MASK) << SCMI_MSG_TOKEN_SHIFT))

#define MAILBOX_MEM_PAYLOAD_SIZE	(0x80)
#define MAILBOX_SECURE_PSCI_CHANNEL	(0x1)

/*
 * Private data structure for representing the mailbox memory layout. Refer
 * the SCMI specification for more details.
 */
typedef struct mailbox_mem {
        uint32_t res_a; /* Reserved */
        volatile uint32_t status;
        uint64_t res_b; /* Reserved */
        uint32_t flags;
        volatile uint32_t len;
        volatile uint32_t msg_header;
        uint32_t payload[];
} mailbox_mem_t;

static inline void validate_scmi_channel(scmi_channel_t *ch)
{
        if (!ch || !ch->is_initialized)
		sbi_hart_hang();

        if (!ch->info || !ch->info->scmi_mbx_mem)
		sbi_hart_hang();
}

void scmi_send_sync_command(scmi_channel_t *ch);
void scmi_get_channel(scmi_channel_t *ch);
void scmi_put_channel(scmi_channel_t *ch);

#endif
