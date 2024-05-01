#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020 Western Digital Corporation or its affiliates.
#
# Authors:
#   Anup Patel <anup.patel@wdc.com>
#

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/common/arm_pm.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/css/common/css_pm.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/css/scmi/scmi_common.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/css/scmi/scmi_pwr_dmn_proto.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/css/scmi/scmi_sys_pwr_proto.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/css/scp/css_pm_scmi.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/css/mhu/css_mhu_doorbell.o

libsbiutils-objs-$(CONFIG_ARM_SCMI_PROTOCOL_SUPPORT) += arm_scmi/board/spacemit/spacemit_pm.o
