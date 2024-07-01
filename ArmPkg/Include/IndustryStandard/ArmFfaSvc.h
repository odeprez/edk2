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
#define ARM_SVC_ID_FFA_RXTX_MAP_AARCH32              0x84000066
#define ARM_SVC_ID_FFA_RXTX_MAP_AARCH64              0xC4000066
#define ARM_SVC_ID_FFA_RX_RELEASE_AARCH32            0x84000065
#define ARM_SVC_ID_FFA_RXTX_UNMAP_AARCH32            0x84000067
#define ARM_SVC_ID_FFA_PARTITION_INFO_GET_AARCH32    0x84000068
#define ARM_SVC_ID_FFA_ID_GET_AARCH32                0x84000069
#define ARM_SVC_ID_FFA_RUN_AARCH32                   0x8400006D
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH32   0x8400006F
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP_AARCH32  0x84000070
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH64   0xC400006F
#define ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP_AARCH64  0xC4000070
#define ARM_SVC_ID_FFA_SUCCESS_AARCH32               0x84000061
#define ARM_SVC_ID_FFA_SUCCESS_AARCH64               0xC4000061
#define ARM_SVC_ID_FFA_MEM_PERM_SET_AARCH32          0x84000089
#define ARM_SVC_ID_FFA_MEM_PERM_GET_AARCH32          0x84000088
#define ARM_SVC_ID_FFA_INTERRUPT_AARCH32             0x84000062
#define ARM_SVC_ID_FFA_ERROR_AARCH32                 0x84000060
#define ARM_SVC_ID_FFA_ERROR_AARCH64                 0xC4000060
#define ARM_SVC_ID_FFA_MSG_WAIT_AARCH32              0x8400006B

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
#define SPM_MINOR_VERSION_FFA  1

#define ARM_FFA_SPM_RET_SUCCESS              0
#define ARM_FFA_SPM_RET_NOT_SUPPORTED       -1
#define ARM_FFA_SPM_RET_INVALID_PARAMETERS  -2
#define ARM_FFA_SPM_RET_NO_MEMORY           -3
#define ARM_FFA_SPM_RET_BUSY                -4
#define ARM_FFA_SPM_RET_INTERRUPTED         -5
#define ARM_FFA_SPM_RET_DENIED              -6
#define ARM_FFA_SPM_RET_RETRY               -7
#define ARM_FFA_SPM_RET_ABORTED             -8

// FF-A version helper macros
#define FFA_VERSION_MAJOR_SHIFT             16
#define FFA_VERSION_MAJOR_MASK				      0x7FFF
#define FFA_VERSION_MINOR_SHIFT         		0
#define FFA_VERSION_MINOR_MASK          		0xFFFF
#define FFA_VERSION_BIT31_MASK          		(0x1u << 31)

#define MAKE_FFA_VERSION(major, minor)  					\
	((((major) & FFA_VERSION_MAJOR_MASK) << FFA_VERSION_MAJOR_SHIFT) |	\
	 (((minor) & FFA_VERSION_MINOR_MASK) << FFA_VERSION_MINOR_SHIFT))

#define FFA_VERSION_COMPILED            MAKE_FFA_VERSION(SPM_MAJOR_VERSION_FFA, \
                                        SPM_MINOR_VERSION_FFA)

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

// FF-A partition information descriptor
typedef struct {
  UINT16 PartId;
  UINT16 EcCnt;
  UINT32 PartProps;
  UINT32 PartGuid[4];
} EFI_FFA_PART_INFO_DESC;

#define PART_INFO_PROP_MASK                    0x3f
#define PART_INFO_PROP_SHIFT                   0
#define PART_INFO_PROP_DIR_MSG_RECV_BIT        (1u << 0)
#define PART_INFO_PROP_DIR_MSG_SEND_BIT        (1u << 1)
#define PART_INFO_PROP_INDIR_MSG_BIT           (1u << 2)
#define PART_INFO_PROP_NOTIFICATIONS_BIT       (1u << 3)
#define PART_INFO_PROP_EP_TYPE_MASK            0x3
#define PART_INFO_PROP_EP_TYPE_SHIFT           4
#define PART_INFO_PROP_EP_PE                   0
#define PART_INFO_PROP_EP_SEPID_IND            1
#define PART_INFO_PROP_EP_SEPID_DEP            2
#define PART_INFO_PROP_EP_AUX                  3

#endif // ARM_FFA_SVC_H_
