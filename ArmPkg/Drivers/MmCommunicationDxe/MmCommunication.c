/** @file

  Copyright (c) 2016-2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/ArmLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/MmCommunication2.h>

#include <IndustryStandard/ArmFfaSvc.h>
#include <IndustryStandard/ArmStdSmc.h>

#include "MmCommunicate.h"

//
// Partition ID if FF-A support is enabled
//
STATIC UINT16  mFfaPartId;

// Partition information of the StMM SP if FF-A support is enabled
// TODO: Revisit assumption that there is only a single StMM SP
//
STATIC EFI_FFA_PART_INFO_DESC mStmmPartInfo;

//
// RX/TX pair if FF-A support is enabled
//
STATIC UINT8 FfaRxBuf[EFI_PAGE_SIZE] __attribute__ ((aligned (EFI_PAGE_SIZE)));
STATIC UINT8 FfaTxBuf[EFI_PAGE_SIZE] __attribute__ ((aligned (EFI_PAGE_SIZE)));

//
// Address, Length of the pre-allocated buffer for communication with the secure
// world.
//
STATIC ARM_MEMORY_REGION_DESCRIPTOR  mNsCommBuffMemRegion;

// Notification event when virtual address map is set.
STATIC EFI_EVENT  mSetVirtualAddressMapEvent;

// Notification event when exit boot services is called.
STATIC EFI_EVENT  mExitBootServicesEvent;

//
// Handle to install the MM Communication Protocol
//
STATIC EFI_HANDLE  mMmCommunicateHandle;

/**
  Communicates with a registered handler.

  This function provides a service to send and receive messages from a registered UEFI service.

  @param[in] This                     The EFI_MM_COMMUNICATION_PROTOCOL instance.
  @param[in, out] CommBufferPhysical  Physical address of the MM communication buffer
  @param[in, out] CommBufferVirtual   Virtual address of the MM communication buffer
  @param[in, out] CommSize            The size of the data buffer being passed in. On input,
                                      when not omitted, the buffer should cover EFI_MM_COMMUNICATE_HEADER
                                      and the value of MessageLength field. On exit, the size
                                      of data being returned. Zero if the handler does not
                                      wish to reply with any data. This parameter is optional
                                      and may be NULL.

  @retval EFI_SUCCESS            The message was successfully posted.
  @retval EFI_INVALID_PARAMETER  CommBufferPhysical or CommBufferVirtual was NULL, or
                                 integer value pointed by CommSize does not cover
                                 EFI_MM_COMMUNICATE_HEADER and the value of MessageLength
                                 field.
  @retval EFI_BAD_BUFFER_SIZE    The buffer is too large for the MM implementation.
                                 If this error is returned, the MessageLength field
                                 in the CommBuffer header or the integer pointed by
                                 CommSize, are updated to reflect the maximum payload
                                 size the implementation can accommodate.
  @retval EFI_ACCESS_DENIED      The CommunicateBuffer parameter or CommSize parameter,
                                 if not omitted, are in address range that cannot be
                                 accessed by the MM environment.

**/
EFI_STATUS
EFIAPI
MmCommunication2Communicate (
  IN CONST EFI_MM_COMMUNICATION2_PROTOCOL  *This,
  IN OUT VOID                              *CommBufferPhysical,
  IN OUT VOID                              *CommBufferVirtual,
  IN OUT UINTN                             *CommSize OPTIONAL
  )
{
  EFI_MM_COMMUNICATE_HEADER  *CommunicateHeader;
  ARM_SMC_ARGS               CommunicateSmcArgs;
  EFI_STATUS                 Status;
  UINTN                      BufferSize;
  UINTN                      Ret;

  Status     = EFI_ACCESS_DENIED;
  BufferSize = 0;

  ZeroMem (&CommunicateSmcArgs, sizeof (ARM_SMC_ARGS));

  //
  // Check parameters
  //
  if ((CommBufferVirtual == NULL) || (CommBufferPhysical == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status            = EFI_SUCCESS;
  CommunicateHeader = CommBufferVirtual;
  // CommBuffer is a mandatory parameter. Hence, Rely on
  // MessageLength + Header to ascertain the
  // total size of the communication payload rather than
  // rely on optional CommSize parameter
  BufferSize = CommunicateHeader->MessageLength +
               sizeof (CommunicateHeader->HeaderGuid) +
               sizeof (CommunicateHeader->MessageLength);

  // If CommSize is not omitted, perform size inspection before proceeding.
  if (CommSize != NULL) {
    // This case can be used by the consumer of this driver to find out the
    // max size that can be used for allocating CommBuffer.
    if ((*CommSize == 0) ||
        (*CommSize > mNsCommBuffMemRegion.Length))
    {
      *CommSize = mNsCommBuffMemRegion.Length;
      Status    = EFI_BAD_BUFFER_SIZE;
    }

    //
    // CommSize should cover at least MessageLength + sizeof (EFI_MM_COMMUNICATE_HEADER);
    //
    if (*CommSize < BufferSize) {
      Status = EFI_INVALID_PARAMETER;
    }
  }

  //
  // If the message length is 0 or greater than what can be tolerated by the MM
  // environment then return the expected size.
  //
  if ((CommunicateHeader->MessageLength == 0) ||
      (BufferSize > mNsCommBuffMemRegion.Length))
  {
    CommunicateHeader->MessageLength = mNsCommBuffMemRegion.Length -
                                       sizeof (CommunicateHeader->HeaderGuid) -
                                       sizeof (CommunicateHeader->MessageLength);
    Status = EFI_BAD_BUFFER_SIZE;
  }

  // MessageLength or CommSize check has failed, return here.
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Copy Communication Payload
  CopyMem ((VOID *)mNsCommBuffMemRegion.VirtualBase, CommBufferVirtual, BufferSize);

  // Use the FF-A interface if enabled.
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    // FF-A Interface ID for direct message communication
    CommunicateSmcArgs.Arg0 = ARM_SVC_ID_FFA_MSG_SEND_DIRECT_REQ_AARCH64;

    // FF-A Destination EndPoint ID, not used as of now
    CommunicateSmcArgs.Arg1 = mFfaPartId << 16 | mStmmPartInfo.PartId;

    // Reserved for future use(MBZ)
    CommunicateSmcArgs.Arg2 = 0x0;

    // comm_buffer_address (64-bit physical address)
    CommunicateSmcArgs.Arg3 = (UINTN)mNsCommBuffMemRegion.PhysicalBase;

    // Cookie
    CommunicateSmcArgs.Arg4 = 0x0;

    // Not Used
    CommunicateSmcArgs.Arg5 = 0;

    // comm_size_address (not used, indicated by setting to zero)
    CommunicateSmcArgs.Arg6 = 0;
  } else {
    // SMC Function ID
    CommunicateSmcArgs.Arg0 = ARM_SMC_ID_MM_COMMUNICATE_AARCH64;

    // Cookie
    CommunicateSmcArgs.Arg1 = 0;

    // comm_buffer_address (64-bit physical address)
    CommunicateSmcArgs.Arg2 = (UINTN)mNsCommBuffMemRegion.PhysicalBase;

    // comm_size_address (not used, indicated by setting to zero)
    CommunicateSmcArgs.Arg3 = 0;
  }

ffa_intr_loop:
  // Call the Standalone MM environment.
  ArmCallSmc (&CommunicateSmcArgs);

  Ret = CommunicateSmcArgs.Arg0;

  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    if (Ret == ARM_SVC_ID_FFA_INTERRUPT_AARCH32) {
      DEBUG ((DEBUG_INFO, "Resuming interrupted FF-A call \n"));

      // FF-A Interface ID for running the interrupted partition
      CommunicateSmcArgs.Arg0 = ARM_SVC_ID_FFA_RUN_AARCH32;

      // FF-A Destination EndPoint and vCPU ID, TODO: We are assuming vCPU0 of the
      // StMM SP since it is UP.
      CommunicateSmcArgs.Arg1 = mStmmPartInfo.PartId << 16;

      // Loop if the call was interrupted
      goto ffa_intr_loop;
    }
  }

  if (((FixedPcdGet32 (PcdFfaEnable) != 0) &&
      (Ret == ARM_SVC_ID_FFA_MSG_SEND_DIRECT_RESP)) ||
      (Ret == ARM_SMC_MM_RET_SUCCESS)) {
    ZeroMem (CommBufferVirtual, BufferSize);
    // On successful return, the size of data being returned is inferred from
    // MessageLength + Header.
    CommunicateHeader = (EFI_MM_COMMUNICATE_HEADER *)mNsCommBuffMemRegion.VirtualBase;
    BufferSize = CommunicateHeader->MessageLength +
                 sizeof (CommunicateHeader->HeaderGuid) +
                 sizeof (CommunicateHeader->MessageLength);

    CopyMem (CommBufferVirtual, (VOID *)mNsCommBuffMemRegion.VirtualBase,
             BufferSize);
    Status = EFI_SUCCESS;
    return Status;
  }

  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    Ret = CommunicateSmcArgs.Arg2;
  }

  // Error Codes are same for FF-A and SMC interface
  switch (Ret) {
  case ARM_SMC_MM_RET_INVALID_PARAMS:
    Status = EFI_INVALID_PARAMETER;
    break;

  case ARM_SMC_MM_RET_DENIED:
    Status = EFI_ACCESS_DENIED;
    break;

  case ARM_SMC_MM_RET_NO_MEMORY:
    // Unexpected error since the CommSize was checked for zero length
    // prior to issuing the SMC
    Status = EFI_OUT_OF_RESOURCES;
    ASSERT (0);
    break;

  default:
    Status = EFI_ACCESS_DENIED;
    ASSERT (0);
  }

  return Status;
}

//
// MM Communication Protocol instance
//
STATIC EFI_MM_COMMUNICATION2_PROTOCOL  mMmCommunication2 = {
  MmCommunication2Communicate
};

/**
  Notification callback on SetVirtualAddressMap event.

  This function notifies the MM communication protocol interface on
  SetVirtualAddressMap event and converts pointers used in this driver
  from physical to virtual address.

  @param  Event          SetVirtualAddressMap event.
  @param  Context        A context when the SetVirtualAddressMap triggered.

  @retval EFI_SUCCESS    The function executed successfully.
  @retval Other          Some error occurred when executing this function.

**/
STATIC
VOID
EFIAPI
NotifySetVirtualAddressMap (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  Status = gRT->ConvertPointer (
                  EFI_OPTIONAL_PTR,
                  (VOID **)&mNsCommBuffMemRegion.VirtualBase
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "NotifySetVirtualAddressMap():"
      " Unable to convert MM runtime pointer. Status:0x%r\n",
      Status
      ));
  }
}

/**
  Notification callback on ExitBootServices event.

  This function notifies the MM communication protocol interface on
  ExitBootServices event and releases the FF-A RX/TX buffer.

  @param  Event          ExitBootServices event.
  @param  Context        A context when the ExitBootServices triggered.

  @retval EFI_SUCCESS    The function executed successfully.
  @retval Other          Some error occurred when executing this function.

**/
STATIC
VOID
EFIAPI
NotifyExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  ARM_SMC_ARGS SmcArgs = {0};

  SmcArgs.Arg0 = ARM_SVC_ID_FFA_RXTX_UNMAP_AARCH32;
  SmcArgs.Arg1 = mFfaPartId << 16;  // TODO: Use a macro for shift
  ArmCallSmc (&SmcArgs);

  // We do not bother checking the error code of the RXTX_UNMAP invocation
  // since we did map the buffers and this call must succeed.
  return;

}

STATIC
EFI_STATUS
GetMmCompatibility (
  )
{
  EFI_STATUS    Status;
  UINT32        MmVersion;
  UINT32        SmccUuid[4];
  ARM_SMC_ARGS  SmcArgs = {0};
  EFI_GUID      MmCommProtGuid = EFI_MM_COMMUNICATION2_PROTOCOL_GUID;

  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    SmcArgs.Arg0 = ARM_SVC_ID_FFA_VERSION_AARCH32;
    SmcArgs.Arg1 = MM_CALLER_MAJOR_VER << MM_MAJOR_VER_SHIFT;
    SmcArgs.Arg1 |= MM_CALLER_MINOR_VER;
  } else {
    // MM_VERSION uses SMC32 calling conventions
    SmcArgs.Arg0 = ARM_SMC_ID_MM_VERSION_AARCH32;
  }

  ArmCallSmc (&SmcArgs);

  MmVersion = SmcArgs.Arg0;

  if ((MM_MAJOR_VER (MmVersion) == MM_CALLER_MAJOR_VER) &&
      (MM_MINOR_VER (MmVersion) >= MM_CALLER_MINOR_VER))
  {
    DEBUG ((
      DEBUG_INFO,
      "MM Version: Major=0x%x, Minor=0x%x\n",
      MM_MAJOR_VER (MmVersion),
      MM_MINOR_VER (MmVersion)
      ));
    Status = EFI_SUCCESS;
  } else {
    DEBUG ((
      DEBUG_ERROR,
      "Incompatible MM Versions.\n Current Version: Major=0x%x, Minor=0x%x.\n Expected: Major=0x%x, Minor>=0x%x.\n",
      MM_MAJOR_VER (MmVersion),
      MM_MINOR_VER (MmVersion),
      MM_CALLER_MAJOR_VER,
      MM_CALLER_MINOR_VER
      ));
    Status = EFI_UNSUPPORTED;
  }

  // If FF-A is supported then discover the StMM SP's presence, ID, our ID and
  // register our RX/TX buffers.
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    EFI_FFA_PART_INFO_DESC *StmmPartInfo;

    // Get our ID
    ZeroMem(&SmcArgs, sizeof(SmcArgs));
    SmcArgs.Arg0 = ARM_SVC_ID_FFA_ID_GET_AARCH32;
    ArmCallSmc (&SmcArgs);
    if (SmcArgs.Arg0 == ARM_SVC_ID_FFA_ERROR_AARCH32) {
      DEBUG ((DEBUG_ERROR, "Unable to retrieve FF-A partition ID (%d).\n", SmcArgs.Arg2));
      return EFI_UNSUPPORTED;
    }
    DEBUG ((DEBUG_INFO, "FF-A partition ID = 0x%lx.\n", SmcArgs.Arg2));
    mFfaPartId = SmcArgs.Arg2;

    // Register our RX/TX pair
    SmcArgs.Arg0 = ARM_SVC_ID_FFA_RXTX_MAP_AARCH64;
    SmcArgs.Arg1 = (UINTN) FfaTxBuf;
    SmcArgs.Arg2 = (UINTN) FfaRxBuf;
    SmcArgs.Arg3 = 1;                  //TODO: Is this a given?
    ArmCallSmc (&SmcArgs);
    if (SmcArgs.Arg0 == ARM_SVC_ID_FFA_ERROR_AARCH32) {
      DEBUG ((DEBUG_ERROR, "Unable to register FF-A RX/TX buffers (%d).\n", SmcArgs.Arg2));
      return EFI_UNSUPPORTED;
    }

    // Discover the StMM SP after converting the EFI_GUID to a format TF-A will
    // understand.
    SmcArgs.Arg0 = ARM_SVC_ID_FFA_PARTITION_INFO_GET_AARCH32;
    MmCommProtGuid.Data2 += MmCommProtGuid.Data3;
    MmCommProtGuid.Data3 = MmCommProtGuid.Data2 - MmCommProtGuid.Data3;
    MmCommProtGuid.Data2 = MmCommProtGuid.Data2 - MmCommProtGuid.Data3;
    CopyMem ((VOID *) SmccUuid, (VOID *) &MmCommProtGuid, sizeof(EFI_GUID));
    SmcArgs.Arg1 = SmccUuid[0];
    SmcArgs.Arg2 = SmccUuid[1];
    SmcArgs.Arg3 = SmccUuid[2];
    SmcArgs.Arg3 = SwapBytes32(SmcArgs.Arg3);
    SmcArgs.Arg4 = SmccUuid[3];
    SmcArgs.Arg4 = SwapBytes32(SmcArgs.Arg4);
    ArmCallSmc (&SmcArgs);
    if (SmcArgs.Arg0 == ARM_SVC_ID_FFA_ERROR_AARCH32) {
      DEBUG ((DEBUG_ERROR, "Unable to discover FF-A StMM SP (%d).\n", SmcArgs.Arg2));
      goto ffa_init_error;
    }

    // Retrieve the partition information from the RX buffer
    StmmPartInfo = (EFI_FFA_PART_INFO_DESC *) FfaRxBuf;

    // TODO: Sanity check the partition type.
    DEBUG ((DEBUG_INFO, "Discovered FF-A StMM SP."));
    DEBUG ((DEBUG_INFO, "ID = 0x%lx, Execution contexts = %d, Properties = 0x%lx. \n",
	    StmmPartInfo->PartId, StmmPartInfo->EcCnt, StmmPartInfo->PartProps));

    // Make a local copy
    mStmmPartInfo = *StmmPartInfo;

    // Release the RX buffer
    ZeroMem(&SmcArgs, sizeof(SmcArgs));
    SmcArgs.Arg0 = ARM_SVC_ID_FFA_RX_RELEASE_AARCH32;
    SmcArgs.Arg1 = mFfaPartId;
    ArmCallSmc (&SmcArgs);

    // This should really never fail since there is only a single CPU booting
    // and another CPU could not have released the RX buffer before us.
    if (SmcArgs.Arg0 == ARM_SVC_ID_FFA_ERROR_AARCH32) {
      DEBUG ((DEBUG_ERROR, "Unable to release FF-A RX buffer (%d).\n", SmcArgs.Arg2));
      ASSERT (0);
      goto ffa_init_error;
    }

    return EFI_SUCCESS;

  ffa_init_error:
    // Release the RX/TX pair before exiting.
    ZeroMem(&SmcArgs, sizeof(SmcArgs));
    SmcArgs.Arg0 = ARM_SVC_ID_FFA_RXTX_UNMAP_AARCH32;
    SmcArgs.Arg1 = mFfaPartId << 16;  // TODO: Use a macro for shift
    ArmCallSmc (&SmcArgs);

    // We do not bother checking the error code of the RXTX_UNMAP invocation
    // since we did map the buffers and this call must succeed.
    return EFI_UNSUPPORTED;
  }

  return Status;
}

STATIC EFI_GUID *CONST  mGuidedEventGuid[] = {
  &gEfiEndOfDxeEventGroupGuid,
  &gEfiEventExitBootServicesGuid,
  &gEfiEventReadyToBootGuid,
};

STATIC EFI_EVENT  mGuidedEvent[ARRAY_SIZE (mGuidedEventGuid)];

/**
  Event notification that is fired when GUIDed Event Group is signaled.

  @param  Event                 The Event that is being processed, not used.
  @param  Context               Event Context, not used.

**/
STATIC
VOID
EFIAPI
MmGuidedEventNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_MM_COMMUNICATE_HEADER  Header;
  UINTN                      Size;

  //
  // Use Guid to initialize EFI_SMM_COMMUNICATE_HEADER structure
  //
  CopyGuid (&Header.HeaderGuid, Context);
  Header.MessageLength = 1;
  Header.Data[0]       = 0;

  Size = sizeof (Header);
  MmCommunication2Communicate (&mMmCommunication2, &Header, &Header, &Size);
}

/**
  The Entry Point for MM Communication

  This function installs the MM communication protocol interface and finds out
  what type of buffer management will be required prior to invoking the
  communication SMC.

  @param  ImageHandle    The firmware allocated handle for the EFI image.
  @param  SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS    The entry point is executed successfully.
  @retval Other          Some error occurred when executing this entry point.

**/
EFI_STATUS
EFIAPI
MmCommunication2Initialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       Index;

  // Check if we can make the MM call
  Status = GetMmCompatibility ();
  if (EFI_ERROR (Status)) {
    goto ReturnErrorStatus;
  }

  mNsCommBuffMemRegion.PhysicalBase = PcdGet64 (PcdMmBufferBase);
  // During boot , Virtual and Physical are same
  mNsCommBuffMemRegion.VirtualBase = mNsCommBuffMemRegion.PhysicalBase;
  mNsCommBuffMemRegion.Length      = PcdGet64 (PcdMmBufferSize);

  ASSERT (mNsCommBuffMemRegion.PhysicalBase != 0);

  ASSERT (mNsCommBuffMemRegion.Length != 0);

  Status = gDS->AddMemorySpace (
                  EfiGcdMemoryTypeReserved,
                  mNsCommBuffMemRegion.PhysicalBase,
                  mNsCommBuffMemRegion.Length,
                  EFI_MEMORY_WB |
                  EFI_MEMORY_XP |
                  EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "MmCommunicateInitialize: "
      "Failed to add MM-NS Buffer Memory Space\n"
      ));
    goto ReturnErrorStatus;
  }

  Status = gDS->SetMemorySpaceAttributes (
                  mNsCommBuffMemRegion.PhysicalBase,
                  mNsCommBuffMemRegion.Length,
                  EFI_MEMORY_WB | EFI_MEMORY_XP | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "MmCommunicateInitialize: "
      "Failed to set MM-NS Buffer Memory attributes\n"
      ));
    goto CleanAddedMemorySpace;
  }

  // Install the communication protocol
  Status = gBS->InstallProtocolInterface (
                  &mMmCommunicateHandle,
                  &gEfiMmCommunication2ProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMmCommunication2
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "MmCommunicationInitialize: "
      "Failed to install MM communication protocol\n"
      ));
    goto CleanAddedMemorySpace;
  }

  // Register notification callback when ExitBootservices is called to
  // unregister the FF-A RX/TX buffer pair. This allows the OS to register its
  // own buffer pair.
  if (FixedPcdGet32 (PcdFfaEnable) != 0) {
    Status = gBS->CreateEvent (
                    EVT_SIGNAL_EXIT_BOOT_SERVICES,
                    TPL_NOTIFY,
                    NotifyExitBootServices,
                    NULL,
                    &mExitBootServicesEvent
                    );
    ASSERT_EFI_ERROR (Status);
  }
  // Register notification callback when virtual address is associated
  // with the physical address.
  // Create a Set Virtual Address Map event.
  Status = gBS->CreateEvent (
                  EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE,
                  TPL_NOTIFY,
                  NotifySetVirtualAddressMap,
                  NULL,
                  &mSetVirtualAddressMapEvent
                  );
  ASSERT_EFI_ERROR (Status);

  for (Index = 0; Index < ARRAY_SIZE (mGuidedEventGuid); Index++) {
    Status = gBS->CreateEventEx (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    MmGuidedEventNotify,
                    mGuidedEventGuid[Index],
                    mGuidedEventGuid[Index],
                    &mGuidedEvent[Index]
                    );
    ASSERT_EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      while (Index-- > 0) {
        gBS->CloseEvent (mGuidedEvent[Index]);
      }

      goto UninstallProtocol;
    }
  }

  return EFI_SUCCESS;

UninstallProtocol:
  gBS->UninstallProtocolInterface (
         mMmCommunicateHandle,
         &gEfiMmCommunication2ProtocolGuid,
         &mMmCommunication2
         );

CleanAddedMemorySpace:
  gDS->RemoveMemorySpace (
         mNsCommBuffMemRegion.PhysicalBase,
         mNsCommBuffMemRegion.Length
         );

ReturnErrorStatus:
  return EFI_INVALID_PARAMETER;
}
