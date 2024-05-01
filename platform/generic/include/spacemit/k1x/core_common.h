#ifndef __K1X_CORE_COMMON_H__
#define __K1X_CORE_COMMON_H__


#define CSR_MHCR	0x7c1
#define CSR_MSETUP	0x7c0
#define CSR_MHINT	0x7c5
#define CSR_ML2SETUP    0x7F0

#define CACHE_LINE_SIZE		(64)
#define CACHE_INV_ADDR_Msk	(0xffffffffffffffff << 6)

#endif /* __K1X_CORE_COMMON_H__ */
