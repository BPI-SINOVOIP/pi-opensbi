/*
 * Copyright (c) 2017-2019, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sbi/sbi_platform.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_scratch.h>
#include <sbi_utils/cache/cacheflush.h>
#include <sbi_utils/psci/drivers/arm/css/scmi.h>
#include <sbi_utils/psci/drivers/arm/css/scmi_private.h>

#define scmi_lock_init(lock)
#define scmi_lock_get(lock)		spin_lock(lock)
#define scmi_lock_release(lock)		spin_unlock(lock)


/*
 * Private helper function to get exclusive access to SCMI channel.
 */
void scmi_get_channel(scmi_channel_t *ch)
{
	if (!ch->lock)
		sbi_hart_hang();

	scmi_lock_get(ch->lock);

	/* Make sure any previous command has finished */
	if (!SCMI_IS_CHANNEL_FREE(
			((mailbox_mem_t *)(ch->info->scmi_mbx_mem))->status))
		sbi_hart_hang();
}

/*
 * Private helper function to transfer ownership of channel from AP to SCP.
 */
void scmi_send_sync_command(scmi_channel_t *ch)
{
	mailbox_mem_t *mbx_mem = (mailbox_mem_t *)(ch->info->scmi_mbx_mem);

	SCMI_MARK_CHANNEL_BUSY(mbx_mem->status);

	/*
	 * Ensure that any write to the SCMI payload area is seen by SCP before
	 * we write to the doorbell register. If these 2 writes were reordered
	 * by the CPU then SCP would read stale payload data
	 */
	/* dmbst(); */
	asm volatile ("fence iorw, iorw");

	ch->info->ring_doorbell(ch->info);
	/*
	 * Ensure that the write to the doorbell register is ordered prior to
	 * checking whether the channel is free.
	 */
	/* dmbsy(); */
	asm volatile ("fence iorw, iorw");

	/* Wait for channel to be free */
	while (!SCMI_IS_CHANNEL_FREE(mbx_mem->status))
		;

	/*
	 * Ensure that any read to the SCMI payload area is done after reading
	 * mailbox status. If these 2 reads were reordered then the CPU would
	 * read invalid payload data
	 */
	/* dmbld(); */
	asm volatile ("fence iorw, iorw");
}

/*
 * Private helper function to release exclusive access to SCMI channel.
 */
void scmi_put_channel(scmi_channel_t *ch)
{
	/* Make sure any previous command has finished */
	if (!SCMI_IS_CHANNEL_FREE(
			((mailbox_mem_t *)(ch->info->scmi_mbx_mem))->status))
		sbi_hart_hang();

	if (!ch->lock)
		sbi_hart_hang();

	scmi_lock_release(ch->lock);
}

/*
 * API to query the SCMI protocol version.
 */
int scmi_proto_version(void *p, uint32_t proto_id, uint32_t *version)
{
	mailbox_mem_t *mbx_mem;
	unsigned int token = 0;
	int ret;
	scmi_channel_t *ch = (scmi_channel_t *)p;

	validate_scmi_channel(ch);

	scmi_get_channel(ch);

	mbx_mem = (mailbox_mem_t *)(ch->info->scmi_mbx_mem);
	mbx_mem->msg_header = SCMI_MSG_CREATE(proto_id, SCMI_PROTO_VERSION_MSG,
							token);
	mbx_mem->len = SCMI_PROTO_VERSION_MSG_LEN;
	mbx_mem->flags = SCMI_FLAG_RESP_POLL;

	csi_dcache_clean_invalid_range((uintptr_t)ch->info->scmi_mbx_mem, 0x80);

	scmi_send_sync_command(ch);

	/* Get the return values */
	SCMI_PAYLOAD_RET_VAL2(mbx_mem->payload, ret, *version);
	if (mbx_mem->len != SCMI_PROTO_VERSION_RESP_LEN) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	if (token != SCMI_MSG_GET_TOKEN(mbx_mem->msg_header)) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	scmi_put_channel(ch);

	return ret;
}

/*
 * API to query the protocol message attributes for a SCMI protocol.
 */
int scmi_proto_msg_attr(void *p, uint32_t proto_id,
		uint32_t command_id, uint32_t *attr)
{
	mailbox_mem_t *mbx_mem;
	unsigned int token = 0;
	int ret;
	scmi_channel_t *ch = (scmi_channel_t *)p;

	validate_scmi_channel(ch);

	scmi_get_channel(ch);

	mbx_mem = (mailbox_mem_t *)(ch->info->scmi_mbx_mem);
	mbx_mem->msg_header = SCMI_MSG_CREATE(proto_id,
				SCMI_PROTO_MSG_ATTR_MSG, token);
	mbx_mem->len = SCMI_PROTO_MSG_ATTR_MSG_LEN;
	mbx_mem->flags = SCMI_FLAG_RESP_POLL;
	SCMI_PAYLOAD_ARG1(mbx_mem->payload, command_id);

	csi_dcache_clean_invalid_range((uintptr_t)ch->info->scmi_mbx_mem, 0x80);
	
	scmi_send_sync_command(ch);

	/* Get the return values */
	SCMI_PAYLOAD_RET_VAL2(mbx_mem->payload, ret, *attr);
	if (mbx_mem->len != SCMI_PROTO_MSG_ATTR_RESP_LEN) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	if (token != SCMI_MSG_GET_TOKEN(mbx_mem->msg_header)) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

	scmi_put_channel(ch);

	return ret;
}

/*
 * SCMI Driver initialization API. Returns initialized channel on success
 * or NULL on error. The return type is an opaque void pointer.
 */
void *scmi_init(scmi_channel_t *ch)
{
	uint32_t version;
	int ret;

	if (!ch || !ch->info || !ch->info->db_reg_addr || !ch->info->db_modify_mask ||
			!ch->info->db_preserve_mask || !ch->info->ring_doorbell ||
			!ch->lock)
		sbi_hart_hang();

	scmi_lock_init(ch->lock);

	ch->is_initialized = 1;

	ret = scmi_proto_version(ch, SCMI_PWR_DMN_PROTO_ID, &version);
	if (ret != SCMI_E_SUCCESS) {
		sbi_printf("SCMI power domain protocol version message failed\n");
		goto error;
	}

	if (!is_scmi_version_compatible(SCMI_PWR_DMN_PROTO_VER, version)) {
		sbi_printf("SCMI power domain protocol version 0x%x incompatible with driver version 0x%x\n",
			version, SCMI_PWR_DMN_PROTO_VER);
		goto error;
	}

	sbi_printf("SCMI power domain protocol version 0x%x detected\n", version);

	ret = scmi_proto_version(ch, SCMI_SYS_PWR_PROTO_ID, &version);
	if ((ret != SCMI_E_SUCCESS)) {
		sbi_printf("SCMI system power protocol version message failed\n");
		goto error;
	}

	if (!is_scmi_version_compatible(SCMI_SYS_PWR_PROTO_VER, version)) {
		sbi_printf("SCMI system power management protocol version 0x%x incompatible with driver version 0x%x\n",
			version, SCMI_SYS_PWR_PROTO_VER);
		goto error;
	}

	sbi_printf("SCMI system power management protocol version 0x%x detected\n",
						version);

	sbi_printf("SCMI driver initialized\n");

	return (void *)ch;

error:
	ch->is_initialized = 0;
	return NULL;
}
