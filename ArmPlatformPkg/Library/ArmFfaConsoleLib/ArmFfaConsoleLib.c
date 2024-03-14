/** @file
  Serial I/O Port library functions with no library constructor/destructor

  Copyright (c) 2022, ARM Ltd. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <Library/ArmSvcLib.h>
#include <Library/SerialPortLib.h>
#include <IndustryStandard/ArmFfaSvc.h>

/**
  Initialise the serial device hardware with default settings.

  @retval RETURN_SUCCESS            The serial device was initialised.
 **/
RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  return RETURN_SUCCESS;
}

/**
  Write data through FFA_CONSOLE_LOG supervisor call.

  @param  Buffer           Point of data buffer which need to be written.
  @param  NumberOfBytes    Number of output bytes which are cached in Buffer.

  @retval 0                Write data failed.
  @retval !0               Actual number of bytes written.
**/
UINTN
EFIAPI
SerialPortWrite (
  IN UINT8     *Buffer,
  IN UINTN     NumberOfBytes
  )
{
  const UINTN FfaConsoleLogNbRegs64 = 6;
  const UINTN MaxCharsPerSvcCall = sizeof(UINT64) * FfaConsoleLogNbRegs64;
  UINTN RegIndex, NbSvcCalls, BufferIndex, LoopInner;
  UINTN NumberOfBytesInner, NumberOfCharsRemaining;
  UINT64 Regs[FfaConsoleLogNbRegs64];
  ARM_SVC_ARGS Args;

  BufferIndex = 0;
  NumberOfCharsRemaining = NumberOfBytes;
  for (NbSvcCalls = 0; NbSvcCalls < 1 + (NumberOfBytes / MaxCharsPerSvcCall); NbSvcCalls++) {
    for (LoopInner = 0; LoopInner < FfaConsoleLogNbRegs64; LoopInner++) {
      Regs[LoopInner] = 0;
    }

    NumberOfBytesInner = (NumberOfCharsRemaining > MaxCharsPerSvcCall) ?
      MaxCharsPerSvcCall : NumberOfCharsRemaining;
    for (LoopInner = 0; LoopInner < NumberOfBytesInner; LoopInner++) {
      RegIndex = LoopInner >> 3;
      Regs[RegIndex] |= ((UINT64)Buffer[BufferIndex]) << (sizeof(UINT64) * (BufferIndex % MaxCharsPerSvcCall));
      BufferIndex++;
    }

    Args.Arg0 = ARM_FID_FFA_CONSOLE_LOG;
    Args.Arg1 = NumberOfBytesInner;
    Args.Arg2 = Regs[0];
    Args.Arg3 = Regs[1];
    Args.Arg4 = Regs[2];
    Args.Arg5 = Regs[3];
    Args.Arg6 = Regs[4];
    Args.Arg7 = Regs[5];
    ArmCallSvc(&Args);

    if (Args.Arg0 != ARM_FID_FFA_SUCCESS_AARCH32) {
      /* Note r0 = FFA_ERROR may return r2 = NOT_SUPPORTED/INVALID_PARAMETERS */
      return 0;
    }

    NumberOfCharsRemaining -= NumberOfBytesInner;
  }

  return NumberOfBytes;
}

/**
  Read data from serial device and save the data in buffer.

  @param  Buffer           Point of data buffer which need to be written.
  @param  NumberOfBytes    Number of output bytes which are cached in Buffer.

  @retval 0                Read data failed.

**/
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8     *Buffer,
  IN  UINTN     NumberOfBytes
)
{
  return 0;
}

/**
  @retval FALSE      No data is available to be read
**/
BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

/**
  @retval  RETURN_UNSUPPORTED  The device does not support this operation.
**/
RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  @retval  RETURN_UNSUPPORTED  The device does not support this operation.
**/
RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  @retval  RETURN_UNSUPPORTED  The device does not support this operation.
**/
RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  return RETURN_UNSUPPORTED;
}
