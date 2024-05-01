#
# SPDX-License-Identifier: BSD-2-Clause
#

carray-platform_override_modules-$(CONFIG_PLATFORM_SPACEMIT_K1PRO)$(CONFIG_PLATFORM_SPACEMIT_K1X) += spacemit_k1
platform-objs-$(CONFIG_PLATFORM_SPACEMIT_K1PRO)$(CONFIG_PLATFORM_SPACEMIT_K1X) += spacemit/spacemit_k1.o
firmware-its-$(CONFIG_PLATFORM_SPACEMIT_K1PRO)$(CONFIG_PLATFORM_SPACEMIT_K1X) += spacemit/fw_dynamic.its
