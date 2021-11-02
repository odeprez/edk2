/** @file
  Header file for FF-A ABI's that will be used for
  communication between S-EL0 and the Secure Partition
  Manager(SPM)

  Copyright (c) 2020, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Revision Reference:
    - FF-A Version 1.0


**/

#ifndef ARM_FFA_SVC_H_
#define ARM_FFA_SVC_H_

#define ARM_SVC_ID_FFA_VERSION_AARCH32               0x84000063
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH32   0x8400006F
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP_AARCH32  0x84000070
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH64   0xC400006F
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP_AARCH64  0xC4000070

/* Generic IDs when using AArch32 or AArch64 execution state */
#ifdef MDE_CPU_AARCH64
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ   ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH64
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP  ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP_AARCH64
#endif
#ifdef MDE_CPU_ARM
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ   ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH32
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP  ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP_AARCH32
#endif

#define SPM_MAJOR_VERSION_FFA  1
#define SPM_MINOR_VERSION_FFA  0

#define ARM_FFA_SPM_RET_SUCCESS             0
#define ARM_FFA_SPM_RET_NOT_SUPPORTED       -1
#define ARM_FFA_SPM_RET_INVALID_PARAMETERS  -2
#define ARM_FFA_SPM_RET_NO_MEMORY           -3
#define ARM_FFA_SPM_RET_BUSY                -4
#define ARM_FFA_SPM_RET_INTERRUPTED         -5
#define ARM_FFA_SPM_RET_DENIED              -6
#define ARM_FFA_SPM_RET_RETRY               -7
#define ARM_FFA_SPM_RET_ABORTED             -8

// For now, the destination id to be used in the FF-A calls
// is being hard-coded. Subsequently, support will be added
// to get the endpoint id's dynamically
// This is the endpoint id used by the optee os's implementation
// of the spmc.
// https://github.com/OP-TEE/optee_os/blob/master/core/arch/arm/kernel/stmm_sp.c#L66
#define ARM_FFA_DESTINATION_ENDPOINT_ID  3

/******************************************************************************
 * Boot information protocol as per the FF-A v1.1 spec.
 *****************************************************************************/
#define FFA_INIT_DESC_SIGNATURE			0x00000FFA

/* Boot information type. */
#define FFA_BOOT_INFO_TYPE_STD			0x0U
#define FFA_BOOT_INFO_TYPE_IMPL			0x1U

#define FFA_BOOT_INFO_TYPE_MASK			0x1U
#define FFA_BOOT_INFO_TYPE_SHIFT		0x7U
#define FFA_BOOT_INFO_TYPE(type)		\
	(((type) & FFA_BOOT_INFO_TYPE_MASK)	\
	<< FFA_BOOT_INFO_TYPE_SHIFT)

/* Boot information identifier. */
#define FFA_BOOT_INFO_TYPE_ID_FDT		0x0U
#define FFA_BOOT_INFO_TYPE_ID_HOB		0x1U

#define FFA_BOOT_INFO_TYPE_ID_MASK		0x3FU
#define FFA_BOOT_INFO_TYPE_ID_SHIFT		0x0U
#define FFA_BOOT_INFO_TYPE_ID(type)		\
	(((type) & FFA_BOOT_INFO_TYPE_ID_MASK)	\
	<< FFA_BOOT_INFO_TYPE_ID_SHIFT)

/* Format of Flags Name field. */
#define FFA_BOOT_INFO_FLAG_NAME_STRING		0x0U
#define FFA_BOOT_INFO_FLAG_NAME_UUID		0x1U

#define FFA_BOOT_INFO_FLAG_NAME_MASK		0x3U
#define FFA_BOOT_INFO_FLAG_NAME_SHIFT		0x0U
#define FFA_BOOT_INFO_FLAG_NAME(type)		\
	(((type) & FFA_BOOT_INFO_FLAG_NAME_MASK)\
	<< FFA_BOOT_INFO_FLAG_NAME_SHIFT)

/* Format of Flags Contents field. */
#define FFA_BOOT_INFO_FLAG_CONTENT_ADR		0x0U
#define FFA_BOOT_INFO_FLAG_CONTENT_VAL		0x1U

#define FFA_BOOT_INFO_FLAG_CONTENT_MASK		0x1U
#define FFA_BOOT_INFO_FLAG_CONTENT_SHIFT	0x2U
#define FFA_BOOT_INFO_FLAG_CONTENT(content)		\
	(((content) & FFA_BOOT_INFO_FLAG_CONTENT_MASK)	\
	<< FFA_BOOT_INFO_FLAG_CONTENT_SHIFT)

// Descriptor to pass boot information as per the FF-A v1.1 spec.
typedef struct {
  UINT32 Name[4];
  UINT8 Type;
  UINT8 Reserved;
  UINT16 Flags;
  UINT32 SizeBotInfo;
  UINT64 Content;
} EFI_FFA_BOOT_INFO_DESC;

// Descriptor that contains boot info blobs size, number of desc it cointains
// size of each descriptor and offset to the first descriptor.
typedef struct {
  UINT32 Magic; // 0xFFA^M
  UINT32 Version;
  UINT32 SizeBootInfoBlob;
  UINT32 SizeBootInfoDesc;
  UINT32 CountBootInfoDesc;
  UINT32 OffsetBootInfoDesc;
  UINT64 Reserved;
} EFI_FFA_BOOT_INFO_HEADER;

#endif // ARM_FFA_SVC_H_
