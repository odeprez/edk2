/** @file
  File managing the MMU for ARMv8 architecture in S-EL0

  Copyright (c) 2017 - 2021, Arm Limited. All rights reserved.<BR>
  Copyright (c) 2021, Linaro Limited
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Reference(s):
  - [1] SPM based on the MM interface.
        (https://trustedfirmware-a.readthedocs.io/en/latest/components/
         secure-partition-manager-mm.html)
  - [2] Arm Firmware Framework for Armv8-A, DEN0077A, version 1.0
        (https://developer.arm.com/documentation/den0077/a)
**/

#include <Uefi.h>
#include <IndustryStandard/ArmMmSvc.h>
#include <IndustryStandard/ArmFfaSvc.h>

#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmSvcLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>


/**
  Utility function to determine whether ABIs in FF-A v1.1 to set and get
  memory permissions can be used. Ideally, this should be invoked once in the
  library constructor and set a flag that can be used at runtime. However, the
  StMM Core invokes this library before constructors are called and before the
  StMM image itself is relocated.

  @retval EFI_SUCCESS     The availability of ABIs was correctly determined.
  @retval Other value     Software stack is misconfigured.

**/
STATIC
BOOLEAN
UseFfaMemPermAbis (
  VOID
  )
{
  ARM_SVC_ARGS  SvcArgs;
  UINT32        SpmcFfaVersion;
  UINT16        SpmcMajorVer;
  UINT16        SpmcMinorVer;

  // Nothing to do if FF-A has not be enabled
  if (FixedPcdGet32 (PcdFfaEnable) == 0) {
    return FALSE;
  }

  // Prepare the message parameters.
  ZeroMem (&SvcArgs, sizeof (ARM_SVC_ARGS));
  SvcArgs.Arg0 = ARM_SVC_ID_FFA_VERSION_AARCH32;
  SvcArgs.Arg1 = FFA_VERSION_COMPILED;

  // Invoke the ABI
  ArmCallSvc (&SvcArgs);

  // Check if FF-A is supported and what version
  SpmcFfaVersion = SvcArgs.Arg0;

  // Damn! FF-A is not supported at all even though we specified v1.0 as our
  // version. However, the feature flag has been turned on. This is a
  // misconfigured software stack. So, return an error and assert in a debug build.
  if (SpmcFfaVersion == ARM_FFA_SPM_RET_NOT_SUPPORTED) {
    ASSERT (0);
    return FALSE;
  }

  SpmcMajorVer = (SpmcFfaVersion >> FFA_VERSION_MAJOR_SHIFT) & FFA_VERSION_MAJOR_MASK;
  SpmcMinorVer = (SpmcFfaVersion >> FFA_VERSION_MINOR_SHIFT) & FFA_VERSION_MINOR_MASK;

  if ((SpmcMajorVer == SPM_MAJOR_VERSION_FFA) && (SpmcMinorVer >= SPM_MINOR_VERSION_FFA)) {
    return TRUE;
  }

  return FALSE;
}


/** Send memory permission request to target.

  @param [in, out]  SvcArgs     Pointer to SVC arguments to send. On
                                return it contains the response parameters.
  @param [out]      RetVal      Pointer to return the response value.

  @retval EFI_SUCCESS           Request successfull.
  @retval EFI_INVALID_PARAMETER A parameter is invalid.
  @retval EFI_NOT_READY         Callee is busy or not in a state to handle
                                this request.
  @retval EFI_UNSUPPORTED       This function is not implemented by the
                                callee.
  @retval EFI_ABORTED           Message target ran into an unexpected error
                                and has aborted.
  @retval EFI_ACCESS_DENIED     Access denied.
  @retval EFI_OUT_OF_RESOURCES  Out of memory to perform operation.
**/
STATIC
EFI_STATUS
SendMemoryPermissionRequest (
  IN OUT  ARM_SVC_ARGS  *SvcArgs,
  OUT  INT32            *RetVal
  )
{
  if ((SvcArgs == NULL) || (RetVal == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ArmCallSvc (SvcArgs);
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {

    // Check if FF-A memory permission ABIs were used.
    if (UseFfaMemPermAbis()) {
      switch (SvcArgs->Arg0) {

        case ARM_SVC_ID_FFA_ERROR_AARCH32:
        case ARM_SVC_ID_FFA_ERROR_AARCH64:
          switch (SvcArgs->Arg2) {
          case ARM_FFA_SPM_RET_INVALID_PARAMETERS:
            return EFI_INVALID_PARAMETER;
          case ARM_FFA_SPM_RET_NOT_SUPPORTED:
            return EFI_UNSUPPORTED;
          default:
            // Undefined error code received.
            ASSERT (0);
            return EFI_INVALID_PARAMETER;
          }

        case ARM_SVC_ID_FFA_SUCCESS_AARCH32:
        case ARM_SVC_ID_FFA_SUCCESS_AARCH64:
          *RetVal = SvcArgs->Arg2;
          return EFI_SUCCESS;

        default:
          // Undefined error code received.
          ASSERT (0);
          return EFI_INVALID_PARAMETER;
      }
    }

    // Get/Set memory attributes is an atomic call, with
    // StandaloneMm at S-EL0 being the caller and the SPM
    // core being the callee. Thus there won't be a
    // FFA_INTERRUPT or FFA_SUCCESS response to the Direct
    // Request sent above. This will have to be considered
    // for other Direct Request calls which are not atomic
    // We therefore check only for Direct Response by the
    // callee.
    if (SvcArgs->Arg0 == ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP) {
      // A Direct Response means FF-A success
      // Now check the payload for errors
      // The callee sends back the return value
      // in Arg3
      *RetVal = SvcArgs->Arg3;
    } else {
      // If Arg0 is not a Direct Response, that means we
      // have an FF-A error. We need to check Arg2 for the
      // FF-A error code.
      // See [2], Table 10.8: FFA_ERROR encoding.
      *RetVal = SvcArgs->Arg2;
      switch (*RetVal) {
        case ARM_FFA_SPM_RET_INVALID_PARAMETERS:
          return EFI_INVALID_PARAMETER;

        case ARM_FFA_SPM_RET_DENIED:
          return EFI_ACCESS_DENIED;

        case ARM_FFA_SPM_RET_NOT_SUPPORTED:
          return EFI_UNSUPPORTED;

        case ARM_FFA_SPM_RET_BUSY:
          return EFI_NOT_READY;

        case ARM_FFA_SPM_RET_ABORTED:
          return EFI_ABORTED;

        default:
          // Undefined error code received.
          ASSERT (0);
          return EFI_INVALID_PARAMETER;
      }
    }
  } else {
    *RetVal = SvcArgs->Arg0;
  }

  // Check error response from Callee.
  if ((*RetVal & BIT31) != 0) {
    // Bit 31 set means there is an error returned
    // See [1], Section 13.5.5.1 MM_SP_MEMORY_ATTRIBUTES_GET_AARCH64 and
    // Section 13.5.5.2 MM_SP_MEMORY_ATTRIBUTES_SET_AARCH64.
    switch (*RetVal) {
      case ARM_SVC_SPM_RET_NOT_SUPPORTED:
        return EFI_UNSUPPORTED;

      case ARM_SVC_SPM_RET_INVALID_PARAMS:
        return EFI_INVALID_PARAMETER;

      case ARM_SVC_SPM_RET_DENIED:
        return EFI_ACCESS_DENIED;

      case ARM_SVC_SPM_RET_NO_MEMORY:
        return EFI_OUT_OF_RESOURCES;

      default:
        // Undefined error code received.
        ASSERT (0);
        return EFI_INVALID_PARAMETER;
    }
  }

  return EFI_SUCCESS;
}

/** Request the permission attributes of a memory region from S-EL0.

  @param [in]   BaseAddress          Base address for the memory region.
  @param [out]  MemoryAttributes     Pointer to return the memory attributes.

  @retval EFI_SUCCESS             Request successfull.
  @retval EFI_INVALID_PARAMETER   A parameter is invalid.
  @retval EFI_NOT_READY           Callee is busy or not in a state to handle
                                  this request.
  @retval EFI_UNSUPPORTED         This function is not implemented by the
                                  callee.
  @retval EFI_ABORTED             Message target ran into an unexpected error
                                  and has aborted.
  @retval EFI_ACCESS_DENIED       Access denied.
  @retval EFI_OUT_OF_RESOURCES    Out of memory to perform operation.
**/
STATIC
EFI_STATUS
GetMemoryPermissions (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  OUT UINT32                *MemoryAttributes
  )
{
  EFI_STATUS    Status;
  INT32         Ret;
  ARM_SVC_ARGS  SvcArgs;

  if (MemoryAttributes == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Prepare the message parameters.
  // See [1], Section 13.5.5.1 MM_SP_MEMORY_ATTRIBUTES_GET_AARCH64.
  ZeroMem (&SvcArgs, sizeof (ARM_SVC_ARGS));
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    // Check if FF-A memory permission ABIs can be used.
    if (UseFfaMemPermAbis()) {
      SvcArgs.Arg0 = ARM_SVC_ID_FFA_MEM_PERM_GET_AARCH32;
      SvcArgs.Arg1 = BaseAddress;
    } else {
      // See [2], Section 10.2 FFA_MSG_SEND_DIRECT_REQ.
      SvcArgs.Arg0 = ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ;
      SvcArgs.Arg1 = ARM_FFA_DESTINATION_ENDPOINT_ID;
      SvcArgs.Arg2 = 0;
      SvcArgs.Arg3 = ARM_SVC_ID_SP_GET_MEM_ATTRIBUTES;
      SvcArgs.Arg4 = BaseAddress;
    }
  } else {
    SvcArgs.Arg0 = ARM_SVC_ID_SP_GET_MEM_ATTRIBUTES;
    SvcArgs.Arg1 = BaseAddress;
    SvcArgs.Arg2 = 0;
    SvcArgs.Arg3 = 0;
  }

  Status = SendMemoryPermissionRequest (&SvcArgs, &Ret);
  if (EFI_ERROR (Status)) {
    *MemoryAttributes = 0;
    return Status;
  }

  *MemoryAttributes = Ret;
  return Status;
}

/** Set the permission attributes of a memory region from S-EL0.

  @param [in]  BaseAddress     Base address for the memory region.
  @param [in]  Length          Length of the memory region.
  @param [in]  Permissions     Memory access controls attributes.

  @retval EFI_SUCCESS             Request successfull.
  @retval EFI_INVALID_PARAMETER   A parameter is invalid.
  @retval EFI_NOT_READY           Callee is busy or not in a state to handle
                                  this request.
  @retval EFI_UNSUPPORTED         This function is not implemented by the
                                  callee.
  @retval EFI_ABORTED             Message target ran into an unexpected error
                                  and has aborted.
  @retval EFI_ACCESS_DENIED       Access denied.
  @retval EFI_OUT_OF_RESOURCES    Out of memory to perform operation.
**/
STATIC
EFI_STATUS
RequestMemoryPermissionChange (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN  UINT64                Length,
  IN  UINT32                Permissions
  )
{
  INT32         Ret;
  ARM_SVC_ARGS  SvcArgs;

  // Prepare the message parameters.
  // See [1], Section 13.5.5.2 MM_SP_MEMORY_ATTRIBUTES_SET_AARCH64.
  ZeroMem (&SvcArgs, sizeof (ARM_SVC_ARGS));
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    // Check if FF-A memory permission ABIs can be used.
    if (UseFfaMemPermAbis()) {
      SvcArgs.Arg0 = ARM_SVC_ID_FFA_MEM_PERM_SET_AARCH32;
      SvcArgs.Arg1 = BaseAddress;
      SvcArgs.Arg2 = EFI_SIZE_TO_PAGES (Length);
      SvcArgs.Arg3 = Permissions;
    } else {
      // See [2], Section 10.2 FFA_MSG_SEND_DIRECT_REQ.
      SvcArgs.Arg0 = ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ;
      SvcArgs.Arg1 = ARM_FFA_DESTINATION_ENDPOINT_ID;
      SvcArgs.Arg2 = 0;
      SvcArgs.Arg3 = ARM_SVC_ID_SP_SET_MEM_ATTRIBUTES;
      SvcArgs.Arg4 = BaseAddress;
      SvcArgs.Arg5 = EFI_SIZE_TO_PAGES (Length);
      SvcArgs.Arg6 = Permissions;
    }
  } else {
    SvcArgs.Arg0 = ARM_SVC_ID_SP_SET_MEM_ATTRIBUTES;
    SvcArgs.Arg1 = BaseAddress;
    SvcArgs.Arg2 = EFI_SIZE_TO_PAGES (Length);
    SvcArgs.Arg3 = Permissions;
  }

  return SendMemoryPermissionRequest (&SvcArgs, &Ret);
}

EFI_STATUS
ArmSetMemoryRegionNoExec (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN  UINT64                Length
  )
{
  EFI_STATUS  Status;
  UINT32      MemoryAttributes;
  UINT32      CodePermission;

  Status = GetMemoryPermissions (BaseAddress, &MemoryAttributes);
  if (!EFI_ERROR (Status)) {
    CodePermission = SET_MEM_ATTR_CODE_PERM_XN << SET_MEM_ATTR_CODE_PERM_SHIFT;
    return RequestMemoryPermissionChange (
             BaseAddress,
             Length,
             MemoryAttributes | CodePermission
             );
  }

  return Status;
}

EFI_STATUS
ArmClearMemoryRegionNoExec (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN  UINT64                Length
  )
{
  EFI_STATUS  Status;
  UINT32      MemoryAttributes;
  UINT32      CodePermission;

  Status = GetMemoryPermissions (BaseAddress, &MemoryAttributes);
  if (!EFI_ERROR (Status)) {
    CodePermission = SET_MEM_ATTR_CODE_PERM_XN << SET_MEM_ATTR_CODE_PERM_SHIFT;
    return RequestMemoryPermissionChange (
             BaseAddress,
             Length,
             MemoryAttributes & ~CodePermission
             );
  }

  return Status;
}

EFI_STATUS
ArmSetMemoryRegionReadOnly (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN  UINT64                Length
  )
{
  EFI_STATUS  Status;
  UINT32      MemoryAttributes;
  UINT32      DataPermission;

  Status = GetMemoryPermissions (BaseAddress, &MemoryAttributes);
  if (!EFI_ERROR (Status)) {
    DataPermission = SET_MEM_ATTR_DATA_PERM_RO << SET_MEM_ATTR_DATA_PERM_SHIFT;
    return RequestMemoryPermissionChange (
             BaseAddress,
             Length,
             MemoryAttributes | DataPermission
             );
  }

  return Status;
}

EFI_STATUS
ArmClearMemoryRegionReadOnly (
  IN  EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN  UINT64                Length
  )
{
  EFI_STATUS  Status;
  UINT32      MemoryAttributes;
  UINT32      PermissionRequest;

  Status = GetMemoryPermissions (BaseAddress, &MemoryAttributes);
  if (!EFI_ERROR (Status)) {
    PermissionRequest = SET_MEM_ATTR_MAKE_PERM_REQUEST (
                          SET_MEM_ATTR_DATA_PERM_RW,
                          MemoryAttributes
                          );
    return RequestMemoryPermissionChange (
             BaseAddress,
             Length,
             PermissionRequest
             );
  }

  return Status;
}
