/** @file
  MM protocol to get the error source descriptor information from standalone MM.

  MM Drivers must implement this protocol in order to publish error source
  descriptor information to OSPM through the HEST ACPI table.

  Copyright (c) 2020 - 2021, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MM_HEST_ERROR_SOURCE_DESC_PROTOCOL_H_
#define MM_HEST_ERROR_SOURCE_DESC_PROTOCOL_H_

#define MM_HEST_ERROR_SOURCE_DESC_PROTOCOL_GUID \
  { \
    0x560bf236, 0xa4a8, 0x4d69, { 0xbc, 0xf6, 0xc2, 0x97, 0x24, 0x10, 0x9d, 0x91 } \
  }

typedef struct MmHestErrorSourceDescProtocol
                 EDKII_MM_HEST_ERROR_SOURCE_DESC_PROTOCOL;

/**
  Get HEST Error Source Descriptors from Standalone MM.

  The MM drivers implementing this protocol must convey the total count and
  total length of the error sources the driver has along with the actual error
  source descriptor(s).

  Calling this protocol with Buffer parameter set to NULL shall return
  EFI_BUFFER_TOO_SMALL with the total length and count of the error source
  descriptor(s) it supports. So the caller can allocate Buffer of sufficient
  size and call again.

  @param[in]     This                EDKII_MM_HEST_ERROR_SOURCE_DESC_PROTOCOL
                                     instance.
  @param[out]    Buffer              Buffer to be appended with the error
                                     source descriptors information.
  @param[in,out] ErrorSourcesLength  Total length of all the error source
                                     descriptors.
  @param[in,out] ErrorSourceCount    Count of total error source descriptors
                                     supported by the driver.

  @retval EFI_SUCCESS            If the Buffer is valid and is filled with valid
                                 Error Source descriptor data.
  @retval EFI_BUFFER_TOO_SMALL   Buffer is NULL.
  @retval EFI_INVALID_PARAMETER  ErrorSourcesLength or ErrorSourcesCount is
                                 NULL.
  @retval Other                  If no error source descriptor information is
                                 available.
**/
typedef
EFI_STATUS
(EFIAPI *EDKII_MM_HEST_GET_ERROR_SOURCE_DESCRIPTORS) (
  IN      EDKII_MM_HEST_ERROR_SOURCE_DESC_PROTOCOL *This,
  OUT     VOID                                     **Buffer,
  IN  OUT UINTN                                    *ErrorSourcesLength,
  IN  OUT UINTN                                    *ErrorSourcesCount
  );

//
// Protocol declaration.
//
struct MmHestErrorSourceDescProtocol {
  EDKII_MM_HEST_GET_ERROR_SOURCE_DESCRIPTORS GetHestErrorSourceDescriptors;
};

extern EFI_GUID gMmHestErrorSourceDescProtocolGuid;

#endif // MM_HEST_ERROR_SOURCE_DESC_PROTOCOL_H_
