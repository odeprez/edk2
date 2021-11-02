/** @file
  Entry point to the Standalone MM Foundation when initialized during the SEC
  phase on ARM platforms

Copyright (c) 2017 - 2021, Arm Ltd. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiMm.h>

#include <Library/Arm/StandaloneMmCoreEntryPoint.h>

#include <PiPei.h>
#include <Guid/MmramMemoryReserve.h>
#include <Guid/MpInformation.h>

#include <libfdt.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmSvcLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SerialPortLib.h>
#include <Library/StandaloneMmMmuLib.h>
#include <Library/PcdLib.h>

#include <IndustryStandard/ArmStdSmc.h>
#include <IndustryStandard/ArmMmSvc.h>
#include <IndustryStandard/ArmFfaSvc.h>

#define SPM_MAJOR_VER_MASK   0xFFFF0000
#define SPM_MINOR_VER_MASK   0x0000FFFF
#define SPM_MAJOR_VER_SHIFT  16

#define SPM_MAJOR_VER		  0
#define SPM_MINOR_VER		  1

#define BOOT_PAYLOAD_VERSION  1

#define FFA_PAGE_4K 0
#define FFA_PAGE_16K 1
#define FFA_PAGE_64K 2

// Local variable to help Standalone MM Core decide whether FF-A ABIs can be
// used for all communication. This variable is usable only after the StMM image
// has been relocated and all image section permissions have been correctly
// updated.
STATIC BOOLEAN mUseOnlyFfaAbis = FALSE;

PI_MM_ARM_TF_CPU_DRIVER_ENTRYPOINT  CpuDriverEntryPoint = NULL;

/**
  Retrieve a pointer to and print the boot information passed by privileged
  secure firmware.

  @param  [in] SharedBufAddress   The pointer memory shared with privileged
                                  firmware.

**/
EFI_SECURE_PARTITION_BOOT_INFO *
GetAndPrintBootinformation (
  IN VOID  *SharedBufAddress
  )
{
  EFI_SECURE_PARTITION_BOOT_INFO  *PayloadBootInfo;
  EFI_SECURE_PARTITION_CPU_INFO   *PayloadCpuInfo;
  UINTN                           Index;

  PayloadBootInfo = (EFI_SECURE_PARTITION_BOOT_INFO *)SharedBufAddress;

  if (PayloadBootInfo == NULL) {
    DEBUG ((DEBUG_ERROR, "PayloadBootInfo NULL\n"));
    return NULL;
  }

  if (PayloadBootInfo->Header.Version != BOOT_PAYLOAD_VERSION) {
    DEBUG ((
      DEBUG_ERROR,
      "Boot Information Version Mismatch. Current=0x%x, Expected=0x%x.\n",
      PayloadBootInfo->Header.Version,
      BOOT_PAYLOAD_VERSION
      ));
    return NULL;
  }

  DEBUG ((DEBUG_INFO, "NumSpMemRegions - 0x%x\n", PayloadBootInfo->NumSpMemRegions));
  DEBUG ((DEBUG_INFO, "SpMemBase       - 0x%lx\n", PayloadBootInfo->SpMemBase));
  DEBUG ((DEBUG_INFO, "SpMemLimit      - 0x%lx\n", PayloadBootInfo->SpMemLimit));
  DEBUG ((DEBUG_INFO, "SpImageBase     - 0x%lx\n", PayloadBootInfo->SpImageBase));
  DEBUG ((DEBUG_INFO, "SpStackBase     - 0x%lx\n", PayloadBootInfo->SpStackBase));
  DEBUG ((DEBUG_INFO, "SpHeapBase      - 0x%lx\n", PayloadBootInfo->SpHeapBase));
  DEBUG ((DEBUG_INFO, "SpNsCommBufBase - 0x%lx\n", PayloadBootInfo->SpNsCommBufBase));
  DEBUG ((DEBUG_INFO, "SpSharedBufBase - 0x%lx\n", PayloadBootInfo->SpSharedBufBase));

  DEBUG ((DEBUG_INFO, "SpImageSize     - 0x%x\n", PayloadBootInfo->SpImageSize));
  DEBUG ((DEBUG_INFO, "SpPcpuStackSize - 0x%x\n", PayloadBootInfo->SpPcpuStackSize));
  DEBUG ((DEBUG_INFO, "SpHeapSize      - 0x%x\n", PayloadBootInfo->SpHeapSize));
  DEBUG ((DEBUG_INFO, "SpNsCommBufSize - 0x%x\n", PayloadBootInfo->SpNsCommBufSize));
  DEBUG ((DEBUG_INFO, "SpSharedBufSize - 0x%x\n", PayloadBootInfo->SpSharedBufSize));

  DEBUG ((DEBUG_INFO, "NumCpus         - 0x%x\n", PayloadBootInfo->NumCpus));
  DEBUG ((DEBUG_INFO, "CpuInfo         - 0x%p\n", PayloadBootInfo->CpuInfo));

  PayloadCpuInfo = (EFI_SECURE_PARTITION_CPU_INFO *)PayloadBootInfo->CpuInfo;

  if (PayloadCpuInfo == NULL) {
    DEBUG ((DEBUG_ERROR, "PayloadCpuInfo NULL\n"));
    return NULL;
  }

  for (Index = 0; Index < PayloadBootInfo->NumCpus; Index++) {
    DEBUG ((DEBUG_INFO, "Mpidr           - 0x%lx\n", PayloadCpuInfo[Index].Mpidr));
    DEBUG ((DEBUG_INFO, "LinearId        - 0x%x\n", PayloadCpuInfo[Index].LinearId));
    DEBUG ((DEBUG_INFO, "Flags           - 0x%x\n", PayloadCpuInfo[Index].Flags));
  }

  return PayloadBootInfo;
}

/**
  An StMM SP implements partial support for FF-A v1.0. The FF-A ABIs are used to
  get and set permissions of memory pages in collaboration with the SPMC and
  signalling completion of initialisation. The original Arm MM communication
  interface is used for communication with the Normal world. A TF-A specific
  interface is used for initialising the SP.

  With FF-A v1.1, the StMM SP uses only FF-A ABIs for initialisation and
  communication. This is subject to support for FF-A v1.1 in the SPMC. If this
  is not the case, the StMM implementation reverts to the FF-A v1.0
  behaviour. Any of this is applicable only if the feature flag PcdFfaEnable is
  TRUE.

  This function helps the caller determine whether FF-A v1.1 or v1.0 are
  available and if only FF-A ABIs can be used at runtime.
**/
STATIC
EFI_STATUS
CheckFfaCompatibility (BOOLEAN *UseOnlyFfaAbis)
{
  UINT16       SpmcMajorVer;
  UINT16       SpmcMinorVer;
  UINT32       SpmcVersion;
  ARM_SVC_ARGS SpmcVersionArgs = {0};

  // Sanity check in case of a spurious call.
  if (FixedPcdGet32 (PcdFfaEnable) == 0) {
    return EFI_UNSUPPORTED;
  }

  // Send the SPMC our version to see whether it supports the same or not.
  SpmcVersionArgs.Arg0 = ARM_SVC_ID_FFA_VERSION_AARCH32;
  SpmcVersionArgs.Arg1 = FFA_VERSION_COMPILED;

  ArmCallSvc (&SpmcVersionArgs);
  SpmcVersion = SpmcVersionArgs.Arg0;

  // If the SPMC barfs then bail.
  if (SpmcVersion == ARM_FFA_SPM_RET_NOT_SUPPORTED) {
    return EFI_UNSUPPORTED;
  }

  // Extract the SPMC version
  SpmcMajorVer = (SpmcVersion >> FFA_VERSION_MAJOR_SHIFT) & FFA_VERSION_MAJOR_MASK;
  SpmcMinorVer = (SpmcVersion >> FFA_VERSION_MINOR_SHIFT) & FFA_VERSION_MINOR_MASK;

  // If the major versions differ then all bets are off.
  if (SpmcMajorVer != SPM_MAJOR_VERSION_FFA) {
    return EFI_UNSUPPORTED;
  }

  // We advertised v1.1 as our version. If the SPMC supports it, it must return
  // the same or a compatible version. If it does not then FF-A ABIs cannot be
  // used for all communication.
  if (SpmcMinorVer >= SPM_MINOR_VERSION_FFA) {
    *UseOnlyFfaAbis = TRUE;
  } else {
    *UseOnlyFfaAbis = FALSE;
  }

  // We have validated that there is a compatible FF-A
  // implementation. So. return success.
  return EFI_SUCCESS;
}

/**
  A loop to delegated events.

  @param  [in] EventCompleteSvcArgs   Pointer to the event completion arguments.

**/
VOID
EFIAPI
DelegatedEventLoop (
  IN ARM_SVC_ARGS  *EventCompleteSvcArgs
  )
{
  BOOLEAN     FfaEnabled;
  EFI_STATUS  Status;
  UINTN       SvcStatus;

  while (TRUE) {
    ArmCallSvc (EventCompleteSvcArgs);

    DEBUG ((DEBUG_INFO, "Received delegated event\n"));
    DEBUG ((DEBUG_INFO, "X0 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg0));
    DEBUG ((DEBUG_INFO, "X1 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg1));
    DEBUG ((DEBUG_INFO, "X2 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg2));
    DEBUG ((DEBUG_INFO, "X3 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg3));
    DEBUG ((DEBUG_INFO, "X4 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg4));
    DEBUG ((DEBUG_INFO, "X5 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg5));
    DEBUG ((DEBUG_INFO, "X6 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg6));
    DEBUG ((DEBUG_INFO, "X7 :  0x%x\n", (UINT32)EventCompleteSvcArgs->Arg7));

    FfaEnabled = FixedPcdGet32 (PcdFfaEnable != 0);
    if (FfaEnabled) {
      Status = CpuDriverEntryPoint (
                 EventCompleteSvcArgs->Arg0,
                 EventCompleteSvcArgs->Arg6,
                 EventCompleteSvcArgs->Arg3
                 );
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "Failed delegated event 0x%x, Status 0x%x\n",
          EventCompleteSvcArgs->Arg3,
          Status
          ));
      }
    } else {
      Status = CpuDriverEntryPoint (
                 EventCompleteSvcArgs->Arg0,
                 EventCompleteSvcArgs->Arg3,
                 EventCompleteSvcArgs->Arg1
                 );
      if (EFI_ERROR (Status)) {
        DEBUG ((
          DEBUG_ERROR,
          "Failed delegated event 0x%x, Status 0x%x\n",
          EventCompleteSvcArgs->Arg0,
          Status
          ));
      }
    }

    switch (Status) {
      case EFI_SUCCESS:
        SvcStatus = ARM_SVC_SPM_RET_SUCCESS;
        break;
      case EFI_INVALID_PARAMETER:
        SvcStatus = ARM_SVC_SPM_RET_INVALID_PARAMS;
        break;
      case EFI_ACCESS_DENIED:
        SvcStatus = ARM_SVC_SPM_RET_DENIED;
        break;
      case EFI_OUT_OF_RESOURCES:
        SvcStatus = ARM_SVC_SPM_RET_NO_MEMORY;
        break;
      case EFI_UNSUPPORTED:
        SvcStatus = ARM_SVC_SPM_RET_NOT_SUPPORTED;
        break;
      default:
        SvcStatus = ARM_SVC_SPM_RET_NOT_SUPPORTED;
        break;
    }

    if (FfaEnabled) {
      EventCompleteSvcArgs->Arg0 = ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP;
      EventCompleteSvcArgs->Arg1 = 0;
      EventCompleteSvcArgs->Arg2 = 0;
      EventCompleteSvcArgs->Arg3 = ARM_SVC_ID_SP_EVENT_COMPLETE;
      EventCompleteSvcArgs->Arg4 = SvcStatus;
    } else {
      EventCompleteSvcArgs->Arg0 = ARM_SVC_ID_SP_EVENT_COMPLETE;
      EventCompleteSvcArgs->Arg1 = SvcStatus;
    }
  }
}

STATIC
BOOLEAN
CheckDescription (
    IN VOID   * DtbAddress,
    IN INT32    Offset,
    OUT CHAR8 * Description,
    OUT UINT32  Size
    )
{
  CONST CHAR8 * Property;
  INT32 LenP;

  Property = fdt_getprop (DtbAddress, Offset, "description", &LenP);
  if (Property == NULL) {
    return FALSE;
  }

 return CompareMem (Description, Property, MIN(Size, (UINT32)LenP)) == 0;

}

STATIC
EFI_STATUS
ReadProperty32 (
    IN  VOID   * DtbAddress,
    IN  INT32    Offset,
    IN  CHAR8  * Property,
    OUT UINT32 * Value
    )
{
  CONST UINT32 * Property32;

  Property32 =  fdt_getprop (DtbAddress, Offset, Property, NULL);
  if (Property32 == NULL) {
    DEBUG ((
          DEBUG_ERROR,
          "%s: Missing in FF-A boot information manifest\n",
          Property
          ));
    return EFI_INVALID_PARAMETER;
  }

  *Value = fdt32_to_cpu (*Property32);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ReadProperty64 (
    IN  VOID   * DtbAddress,
    IN  INT32    Offset,
    IN  CHAR8  * Property,
    OUT UINT64 * Value
    )
{
  CONST UINT64 * Property64;

  Property64 =  fdt_getprop (DtbAddress, Offset, Property, NULL);
  if (Property64 == NULL) {
    DEBUG ((
          DEBUG_ERROR,
          "%s: Missing in FF-A boot information manifest\n",
          Property
          ));
    return EFI_INVALID_PARAMETER;
  }

  *Value = fdt64_to_cpu (*Property64);

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
ReadRegionInfo (
    IN VOID  *DtbAddress,
    IN INT32  Node,
    IN CHAR8 *Region,
    IN UINTN  RegionStrSize,
    IN UINT32 PageSize,
    OUT UINT64 *Address,
    OUT UINT64 *Size
    )
{
  BOOLEAN FoundBuffer;
  INTN Status = 0;

  FoundBuffer = CheckDescription (
      DtbAddress,
      Node,
      Region,
      RegionStrSize
      );
  if (!FoundBuffer) {
    return FALSE;
  }

  DEBUG ((DEBUG_INFO, "Found Node: %a\n", Region));
  Status = ReadProperty64 (
      DtbAddress,
      Node,
      "base-address",
      Address
      );
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "base-address missing in DTB"));
    return FALSE;
  }
  DEBUG ((
        DEBUG_INFO,
        "base = 0x%llx\n",
        *Address
        ));

  Status = ReadProperty32 (
      DtbAddress,
      Node,
      "pages-count",
      (UINT32*)Size
      );
  if (Status != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "pages-count missing in DTB"));
    return FALSE;
  }

  DEBUG ((DEBUG_ERROR, "pages-count: 0x%lx\n", *Size));

  *Size = *Size * PageSize;
  DEBUG ((
        DEBUG_INFO,
        "Size = 0x%llx\n",
        *Size
        ));

  return TRUE;
}

/**

  Populates FF-A boot information structure.

  This function receives the address of a DTB from which boot information defind
  by FF-A and required to initialize the standalone environment is extracted.

  @param [in, out] StmmBootInfo  Pointer to a pre-allocated boot info structure to be
                                 populated.
  @param [in]      DtbAddress    Address of the Device tree from where boot
                                 information will be fetched.
**/
STATIC
EFI_STATUS
PopulateBootinformation (
  IN  OUT  EFI_STMM_BOOT_INFO *StmmBootInfo,
  IN       VOID              *DtbAddress
)
{
  INTN Status;
  INT32 Offset;
  INT32 Node;
  BOOLEAN FoundNsCommBuffer = FALSE;
  BOOLEAN FoundSharedBuffer = FALSE;
  BOOLEAN FoundHeap = FALSE;
  UINT32 PageSize;

  Offset = fdt_path_offset (DtbAddress, "/");
  DEBUG ((DEBUG_INFO, "Offset  = %d \n", Offset));
  if (Offset < 0) {
    DEBUG ((DEBUG_ERROR, "Missing FF-A boot information in manifest\n"));
    return EFI_NOT_FOUND;
  }

  Status = ReadProperty64 (
      DtbAddress,
      Offset,
      "load-address",
      &StmmBootInfo->SpMemBase
      );
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  DEBUG ((DEBUG_INFO, "sp mem base  = 0x%llx\n", StmmBootInfo->SpMemBase));

  Status = ReadProperty64 (
      DtbAddress,
      Offset,
      "image-size",
      &StmmBootInfo->SpMemSize
      );
  if (Status != EFI_SUCCESS) {
    return Status;
  }
  DEBUG ((DEBUG_INFO, "sp mem size  = 0x%llx\n", StmmBootInfo->SpMemSize));

  Status = ReadProperty32 (DtbAddress, Offset, "xlat-granule", &PageSize);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  /*  EFI_PAGE_SIZE is 4KB */
  switch (PageSize) {
    case FFA_PAGE_4K:
      PageSize = EFI_PAGE_SIZE;
      break;

    case FFA_PAGE_16K:
      PageSize = 4 * EFI_PAGE_SIZE;
      break;

    case FFA_PAGE_64K:
      PageSize = 16 * EFI_PAGE_SIZE;
      break;

    default:
      DEBUG ((DEBUG_ERROR, "Invalid page type = %lu\n", PageSize));
      return EFI_INVALID_PARAMETER;
      break;
  };

  DEBUG ((DEBUG_INFO, "Page Size = 0x%lx\n", PageSize));

  Offset = fdt_subnode_offset_namelen (
      DtbAddress,
      Offset,
      "memory-regions",
      sizeof("memory-regions") - 1
      );
  if (Offset < 1) {
    DEBUG ((
          DEBUG_ERROR,
          "%s: Missing in FF-A boot information manifest\n",
          "memory-regions"
          ));
    return EFI_INVALID_PARAMETER;
  }

  for (
      Node = fdt_first_subnode (DtbAddress, Offset);
      Node > 0;
      Node = fdt_next_subnode (DtbAddress, Node)) {
    if (!FoundNsCommBuffer) {
      FoundNsCommBuffer = ReadRegionInfo (
          DtbAddress,
          Node,
          "ns-comm",
          sizeof ("ns-comm") - 1,
          PageSize,
          &StmmBootInfo->SpNsCommBufBase,
          &StmmBootInfo->SpNsCommBufSize
          );
    }

    if (!FoundHeap) {
      FoundHeap = ReadRegionInfo (
          DtbAddress,
          Node,
          "heap",
          sizeof ("heap") - 1,
          PageSize,
          &StmmBootInfo->SpHeapBase,
          &StmmBootInfo->SpHeapSize
          );
    }

    if (!FoundSharedBuffer) {
      FoundSharedBuffer = ReadRegionInfo (
          DtbAddress,
          Node,
          "shared-buff",
          sizeof ("shared-buff") - 1,
          PageSize,
          &StmmBootInfo->SpSharedBufBase,
          &StmmBootInfo->SpSharedBufSize
          );
    }
  }

  if (!FoundNsCommBuffer) {
    DEBUG ((DEBUG_ERROR, "Failed to find ns-comm buffer info\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (!FoundHeap) {
    DEBUG ((DEBUG_ERROR, "Failed to find heap buffer info\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (!FoundSharedBuffer) {
    DEBUG ((DEBUG_ERROR, "Failed to find shared buffer info\n"));
    return EFI_INVALID_PARAMETER;
  }

  // Populate CPU information under the assumption made in the FF-A spec that
  // this is a uniprocessor SP that is capable of migration. So, it is fine if
  // it sees 0 as both its physical and linear cpu id
  StmmBootInfo->CpuInfo[0].Mpidr = 0;
  StmmBootInfo->CpuInfo[0].LinearId = 0;
  StmmBootInfo->CpuInfo[0].Flags = 0;

  return EFI_SUCCESS;
}

/**
  Query the SPM version, check compatibility and return success if compatible.

  @retval EFI_SUCCESS       SPM versions compatible.
  @retval EFI_UNSUPPORTED   SPM versions not compatible.
**/
STATIC
EFI_STATUS
GetSpmVersion (
  VOID
  )
{
  EFI_STATUS    Status;
  UINT16        SpmMajorVersion;
  UINT16        SpmMinorVersion;
  UINT32        SpmVersion;
  ARM_SVC_ARGS  SpmVersionArgs;

  SpmVersionArgs.Arg0 = ARM_SVC_ID_SPM_VERSION_AARCH32;

  ArmCallSvc (&SpmVersionArgs);

  SpmVersion = SpmVersionArgs.Arg0;

  SpmMajorVersion = ((SpmVersion & SPM_MAJOR_VER_MASK) >> SPM_MAJOR_VER_SHIFT);
  SpmMinorVersion = ((SpmVersion & SPM_MINOR_VER_MASK) >> 0);

  // Different major revision values indicate possibly incompatible functions.
  // For two revisions, A and B, for which the major revision values are
  // identical, if the minor revision value of revision B is greater than
  // the minor revision value of revision A, then every function in
  // revision A must work in a compatible way with revision B.
  // However, it is possible for revision B to have a higher
  // function count than revision A.
  if ((SpmMajorVersion == SPM_MAJOR_VER) &&
      (SpmMinorVersion >= SPM_MINOR_VER)) {
    DEBUG ((DEBUG_INFO, "SPM Version: Major=0x%x, Minor=0x%x\n",
	          SpmMajorVersion, SpmMinorVersion));
    Status = EFI_SUCCESS;
  } else {
    DEBUG ((DEBUG_INFO, "Incompatible SPM Versions.\n"));
    DEBUG ((DEBUG_INFO, "Current Version: Major=0x%x, Minor=0x%x.\n",
            SpmMajorVersion, SpmMinorVersion));
    DEBUG ((DEBUG_INFO, "Expected: Major=0x%x, Minor>=0x%x.\n",
            SPM_MAJOR_VER, SPM_MINOR_VER));
    Status = EFI_UNSUPPORTED;
  }

  return Status;
}

/**
  Initialize parameters to be sent via SVC call.

  @param[out]     InitMmFoundationSvcArgs  Args structure
  @param[out]     Ret                      Return Code

**/
STATIC
VOID
InitArmSvcArgs (
  OUT ARM_SVC_ARGS  *InitMmFoundationSvcArgs,
  OUT INT32         *Ret
  )
{
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {

    // With FF-A v1.1 invoke FFA_MSG_WAIT to signal completion of SP init.
    if (mUseOnlyFfaAbis) {
      InitMmFoundationSvcArgs->Arg0 = ARM_SVC_ID_FFA_MSG_WAIT_AARCH32;
      return;
    }

    InitMmFoundationSvcArgs->Arg0 = ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP;
    InitMmFoundationSvcArgs->Arg1 = 0;
    InitMmFoundationSvcArgs->Arg2 = 0;
    InitMmFoundationSvcArgs->Arg3 = ARM_SVC_ID_SP_EVENT_COMPLETE;
    InitMmFoundationSvcArgs->Arg4 = *Ret;
  } else {
    InitMmFoundationSvcArgs->Arg0 = ARM_SVC_ID_SP_EVENT_COMPLETE;
    InitMmFoundationSvcArgs->Arg1 = *Ret;
  }
}


STATIC
EFI_STATUS
GetSpManifest (
  IN  OUT     UINT64 **SpManifestAddr,
  IN          VOID    *BootInfoAddr
  )
{
  EFI_FFA_BOOT_INFO_HEADER *FfaBootInfo;
  EFI_FFA_BOOT_INFO_DESC   *FfaBootInfoDesc;

  // Paranoid check to avoid an inadvertent NULL pointer dereference.
  if (BootInfoAddr == NULL) {
    DEBUG ((DEBUG_ERROR, "FF-A Boot information is NULL\n"));
    return EFI_INVALID_PARAMETER;
  }

  // Check boot information magic number.
  FfaBootInfo = (EFI_FFA_BOOT_INFO_HEADER *) BootInfoAddr;
  if (FfaBootInfo->Magic != FFA_INIT_DESC_SIGNATURE) {
    DEBUG ((
          DEBUG_ERROR, "FfaBootInfo Magic no. is invalid 0x%ux\n",
          FfaBootInfo->Magic
          ));
    return EFI_INVALID_PARAMETER;
  }


  FfaBootInfoDesc =
    (EFI_FFA_BOOT_INFO_DESC *)((UINT8 *)BootInfoAddr +
        FfaBootInfo->OffsetBootInfoDesc);

  if (FfaBootInfoDesc->Type ==
      (FFA_BOOT_INFO_TYPE(FFA_BOOT_INFO_TYPE_STD) |
      FFA_BOOT_INFO_TYPE_ID(FFA_BOOT_INFO_TYPE_ID_FDT))) {
    *SpManifestAddr = (UINT64 *) FfaBootInfoDesc->Content;
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "SP manifest not found \n"));
  return EFI_NOT_FOUND;
}

/**
  The entry point of Standalone MM Foundation.

  @param  [in]  SharedBufAddress  Pointer to the Buffer between SPM and SP.
  @param  [in]  SharedBufSize     Size of the shared buffer.
  @param  [in]  cookie1           Cookie 1
  @param  [in]  cookie2           Cookie 2

**/
VOID
EFIAPI
ModuleEntryPoint (
  IN VOID    *SharedBufAddress,
  IN UINT64  SharedBufSize,
  IN UINT64  cookie1,
  IN UINT64  cookie2
  )
{
  PE_COFF_LOADER_IMAGE_CONTEXT    ImageContext;
  EFI_SECURE_PARTITION_BOOT_INFO  *PayloadBootInfo;
  EFI_STMM_BOOT_INFO              StmmBootInfo = {0};
  ARM_SVC_ARGS                    InitMmFoundationSvcArgs;
  EFI_STATUS                      Status;
  INT32                           Ret;
  UINT32                          SectionHeaderOffset;
  UINT16                          NumberOfSections;
  VOID                            *HobStart;
  VOID                            *TeData;
  UINTN                           TeDataSize;
  EFI_PHYSICAL_ADDRESS            ImageBase;
  UINT64                          *DtbAddress;
  EFI_FIRMWARE_VOLUME_HEADER      *BfvAddress;
  BOOLEAN                         UseOnlyFfaAbis = FALSE;

  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    Status = CheckFfaCompatibility (&UseOnlyFfaAbis);
  } else {
    // Get Secure Partition Manager Version Information
    Status = GetSpmVersion ();
  }
  if (EFI_ERROR (Status)) {
    goto finish;
  }

  // If only FF-A is used, the DTB address is passed in the Boot information
  // structure. Else, the Boot info is copied from Sharedbuffer.
  if (UseOnlyFfaAbis) {
    Status = GetSpManifest (&DtbAddress, SharedBufAddress);
    if (Status != EFI_SUCCESS) {
      goto finish;
    }

    // Extract boot information from the DTB
    Status = PopulateBootinformation (&StmmBootInfo, (VOID *) DtbAddress);
    if (Status != EFI_SUCCESS) {
      goto finish;
    }

    // Stash the base address of the boot firmware volume
    BfvAddress = (EFI_FIRMWARE_VOLUME_HEADER *) StmmBootInfo.SpMemBase;
  } else {
    PayloadBootInfo = GetAndPrintBootinformation (SharedBufAddress);
    if (PayloadBootInfo == NULL) {
      Status = EFI_UNSUPPORTED;
      goto finish;
    }

    // Stash the base address of the boot firmware volume
    BfvAddress = (EFI_FIRMWARE_VOLUME_HEADER *) PayloadBootInfo->SpImageBase;
  }


  // Locate PE/COFF File information for the Standalone MM core module
  Status = LocateStandaloneMmCorePeCoffData (BfvAddress, &TeData, &TeDataSize);

  if (EFI_ERROR (Status)) {
    goto finish;
  }

  // Obtain the PE/COFF Section information for the Standalone MM core module
  Status = GetStandaloneMmCorePeCoffSections (
             TeData,
             &ImageContext,
             &ImageBase,
             &SectionHeaderOffset,
             &NumberOfSections
             );

  if (EFI_ERROR (Status)) {
    goto finish;
  }

  //
  // ImageBase may deviate from ImageContext.ImageAddress if we are dealing
  // with a TE image, in which case the latter points to the actual offset
  // of the image, whereas ImageBase refers to the address where the image
  // would start if the stripped PE headers were still in place. In either
  // case, we need to fix up ImageBase so it refers to the actual current
  // load address.
  //
  ImageBase += (UINTN)TeData - ImageContext.ImageAddress;

  // Update the memory access permissions of individual sections in the
  // Standalone MM core module
  Status = UpdateMmFoundationPeCoffPermissions (
             &ImageContext,
             ImageBase,
             SectionHeaderOffset,
             NumberOfSections,
             ArmSetMemoryRegionNoExec,
             ArmSetMemoryRegionReadOnly,
             ArmClearMemoryRegionReadOnly
             );

  if (EFI_ERROR (Status)) {
    goto finish;
  }

  if (ImageContext.ImageAddress != (UINTN)TeData) {
    ImageContext.ImageAddress = (UINTN)TeData;
    ArmSetMemoryRegionNoExec (ImageBase, SIZE_4KB);
    ArmClearMemoryRegionReadOnly (ImageBase, SIZE_4KB);

    Status = PeCoffLoaderRelocateImage (&ImageContext);
    ASSERT_EFI_ERROR (Status);
  }

  // Update the global copy now that the image has been relocated.
  mUseOnlyFfaAbis = UseOnlyFfaAbis;

  //
  // Create Hoblist based upon boot information passed by privileged software
  //
  if (UseOnlyFfaAbis) {
    HobStart = CreateHobListFromStmmBootInfo (&CpuDriverEntryPoint, &StmmBootInfo);
  } else {
    HobStart = CreateHobListFromBootInfo (&CpuDriverEntryPoint, PayloadBootInfo);
  }

  //
  // Call the MM Core entry point
  //
  ProcessModuleEntryPointList (HobStart);

  DEBUG ((DEBUG_INFO, "Shared Cpu Driver EP %p\n", (VOID *)CpuDriverEntryPoint));

finish:
  if (Status == RETURN_UNSUPPORTED) {
    Ret = -1;
  } else if (Status == RETURN_INVALID_PARAMETER) {
    Ret = -2;
  } else if (Status == EFI_NOT_FOUND) {
    Ret = -7;
  } else {
    Ret = 0;
  }

  ZeroMem (&InitMmFoundationSvcArgs, sizeof (InitMmFoundationSvcArgs));
  InitArmSvcArgs (&InitMmFoundationSvcArgs, &Ret);
  DelegatedEventLoop (&InitMmFoundationSvcArgs);
}
