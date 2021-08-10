/** @file
  HEST table parser

  Copyright (c) 2021, Arm Limited.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Specification Reference:
    - ACPI 6.3, Table 18-382, Hardware Error Source Table
**/

#include <IndustryStandard/Acpi.h>
#include <Library/UefiLib.h>
#include "AcpiParser.h"
#include "AcpiTableParser.h"
#include "AcpiView.h"

// Local variables
STATIC ACPI_DESCRIPTION_HEADER_INFO mAcpiHdrInfo;
STATIC UINT32                       *HestErrorSourceCount;
STATIC UINT16                       *HestErrorSourceType;

/**
  This function validates the flags field of error source descriptor structure.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateErrorSourceFlags (
  IN UINT8 *Ptr,
  IN VOID  *Context
  )
{
  if (*(UINT8 *)Ptr > 3) {
    IncrementErrorCount ();
    Print (L"\nERROR: Invalid flags field value must be < 3");
  }
}

/**
  This function validates the enabled field of error source descriptor
  structure.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateEnabledField (
  IN UINT8 *Ptr,
  IN VOID  *Context
  )
{
  if (*(UINT8 *)Ptr > 1) {
    IncrementErrorCount ();
    Print (L"\nERROR: Invalid Enabled field value must be either 0 or 1.");
  }
}

/**
  This function validates the Number of Records to Pre-allocate count field of
  error source descriptor structure.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateNumOfRecordsToPreAllocate (
  IN UINT8 *Ptr,
  IN VOID  *Context
  )
{
  if (*(UINT32 *)Ptr < 0) {
    IncrementErrorCount ();
    Print (L"\nERROR: Number of Records to Pre-allocate must be >= 1.");
  }
}

/**
  This function validates the Max Sections Per Record count field of error
  source descriptor structure.

  @param [in] Ptr     Pointer to the start of the field data.
  @param [in] Context Pointer to context specific information e.g. this
                      could be a pointer to the ACPI table header.
**/
STATIC
VOID
EFIAPI
ValidateMaxSectionsPerRecord (
  IN UINT8 *Ptr,
  IN VOID  *Context
  )
{
  if (*(UINT32 *)Ptr < 0) {
    IncrementErrorCount ();
    Print (L"\nERROR: Max Sections Per Record must be >= 1.");
  }
}

/**
  Dumps the Notification Structure fields

  @param [in] Format  Optional format string for tracing the data.
  @param [in] Ptr     Pointer to the start of the buffer.
**/
STATIC
VOID
EFIAPI
DumpNotificationStructure (
  IN CONST CHAR16 *Format OPTIONAL,
  IN UINT8        *Ptr
  )
{
  EFI_ACPI_6_3_HARDWARE_ERROR_NOTIFICATION_STRUCTURE *Attributes;

  Attributes =
    (EFI_ACPI_6_3_HARDWARE_ERROR_NOTIFICATION_STRUCTURE *)Ptr;

  Print (L"\n");
  PrintFieldName (4, L"Type");
  Print (L"%d\n", Attributes->Type);
  if (Attributes->Type >
      EFI_ACPI_6_3_HARDWARE_ERROR_NOTIFICATION_SOFTWARE_DELEGATED_EXCEPTION) {
    IncrementErrorCount ();
    Print (L"\nERROR: Notification Structure Type must be <= 0xB.");
  }
  PrintFieldName (4, L"Length");
  Print (L"%d\n", Attributes->Length);
  PrintFieldName (4, L"Configuration Write Enable");
  Print (L"%d\n", Attributes->ConfigurationWriteEnable);
  PrintFieldName (4, L"Poll Interval");
  Print (L"%d\n", Attributes->PollInterval);
  PrintFieldName (4, L"Vector");
  Print (L"%d\n", Attributes->Vector);
  PrintFieldName (4, L"Switch Polling Threshold Value");
  Print (L"%d\n", Attributes->SwitchToPollingThresholdValue);
  PrintFieldName (4, L"Switch Polling Threshold Window");
  Print (L"%d\n", Attributes->SwitchToPollingThresholdWindow);
  PrintFieldName (4, L"Error Threshold Value");
  Print (L"%d\n", Attributes->ErrorThresholdValue);
  PrintFieldName (4, L"Error Threshold Window");
  Print (L"%d", Attributes->ErrorThresholdWindow);
}

/**
  Heper macro to populate the header fields of error source descriptor in the
  ACPI_PARSER array
**/
#define PARSE_HEST_ERROR_SOURCE_COMMON_HEADER()                              \
  {L"Type", 2, 0, L"%d", NULL, NULL, NULL, NULL},                            \
  {L"Source Id", 2, 2, L"%d", NULL, NULL, NULL, NULL},                       \
  {L"Reserved", 2, 4, L"0x%x", NULL, NULL, NULL, NULL},                      \
  {L"Flags", 1, 6, L"0x%x", NULL, NULL, ValidateErrorSourceFlags, NULL},     \
  {L"Enabled", 1, 7, L"%d", NULL, NULL, ValidateEnabledField, NULL},         \
  {L"Number of Records to Pre-allocate", 4, 8, L"%d", NULL, NULL,            \
    ValidateNumOfRecordsToPreAllocate, NULL},                                \
  {L"Max Sections Per Record", 4, 12, L"%d", NULL, NULL,                     \
    ValidateMaxSectionsPerRecord, NULL}

/**
  Helper macro to populate the GHES type error source descriptor in the
  ACPI_PARSER array.
**/
#define PARSE_HEST_GHES_ERROR_SOURCE()                                       \
  {L"Type", 2, 0, L"%d", NULL, NULL, NULL, NULL},                            \
  {L"Source Id", 2, 2, L"%d", NULL, NULL, NULL, NULL},                       \
  {L"Related Source Id", 2, 4, L"0x%x", NULL, NULL, NULL, NULL},             \
  {L"Flags", 1, 6, L"0x%x", NULL, NULL, NULL, NULL},                         \
  {L"Enabled", 1, 7, L"%d", NULL, NULL, ValidateEnabledField, NULL},         \
  {L"Number of Records to Pre-allocate", 4, 8, L"%d", NULL, NULL,            \
    ValidateNumOfRecordsToPreAllocate, NULL},                                \
  {L"Max Sections Per Record", 4, 12, L"%d", NULL, NULL,                     \
    ValidateMaxSectionsPerRecord, NULL},                                     \
  {L"Max Raw Data Length", 4, 16, L"%d", NULL, NULL, NULL, NULL},            \
  {L"Error Status Address", 12, 20, NULL, DumpGas, NULL, NULL, NULL},        \
  {L"Notification Structure", 28, 32, NULL, DumpNotificationStructure, NULL, \
    NULL, NULL},                                                             \
  {L"Error Status Block Length", 4, 60, L"%d", NULL, NULL, NULL, NULL}

/**
  An ACPI_PARSER array describing the ACPI HEST Table.
**/
STATIC CONST ACPI_PARSER HestParser[] = {
  PARSE_ACPI_HEADER (&mAcpiHdrInfo),
  {L"Error Source Count", 4, 36, L"%d", NULL, (VOID **)&HestErrorSourceCount,
    NULL, NULL},
  // Error Source Descriptor 1
     // Error Source Descriptor Type
     // Error Source Descriptor Data
     // ...
  // Error Source Descriptor 2
     // Error Source Descriptor Type
     // Error Source Descriptor Data
     // ...
  // ....
  // Error Source Descriptor n
     // Error Source Descriptor Type
     // Error Source Descriptor Data
     // ...
};

/**
  An ACPI_PARSER array describing the HEST error source descriptor type.
**/
STATIC CONST ACPI_PARSER HestErrorSourceTypeParser[] = {
  {L"Type", 2, 0, L"%d", NULL, (VOID **)&HestErrorSourceType, NULL, NULL},
};

/**
  An ACPI_PARSER array describing the HEST PCI-X Root Port AER structure.
**/
STATIC CONST ACPI_PARSER HestErrorSourcePciExpressRootPortAerParser[] = {
  PARSE_HEST_ERROR_SOURCE_COMMON_HEADER(),
  {L"Bus", 4, 16, L"%d", NULL, NULL, NULL, NULL},
  {L"Device", 2, 20, L"%d", NULL, NULL, NULL, NULL},
  {L"Function", 2, 22, L"%d", NULL, NULL, NULL, NULL},
  {L"Device Control", 2, 24, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 26, L"%d", NULL, NULL, NULL, NULL},
  {L"Uncorrectable Error Mask", 4, 28, L"%d", NULL, NULL, NULL, NULL},
  {L"Uncorrectable Error Severity", 4, 32, L"%d", NULL, NULL, NULL, NULL},
  {L"Correctable Error Mask", 4, 36, L"%d", NULL, NULL, NULL, NULL},
  {L"Advanced Error Capabilities and Control", 4, 40, L"%d", NULL, NULL, NULL,
    NULL},
  {L"Root Error Command", 4, 44, L"%d", NULL, NULL, NULL, NULL},
};

/**
  An ACPI_PARSER array describing the HEST PCI-X Device AER structure.
**/
STATIC CONST ACPI_PARSER HestErrorSourcePciExpressDeviceAerParser[] = {
  PARSE_HEST_ERROR_SOURCE_COMMON_HEADER(),
  {L"Bus", 4, 16, L"%d", NULL, NULL, NULL, NULL},
  {L"Device", 2, 20, L"%d", NULL, NULL, NULL, NULL},
  {L"Function", 2, 22, L"%d", NULL, NULL, NULL, NULL},
  {L"Device Control", 2, 24, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 26, L"%d", NULL, NULL, NULL, NULL},
  {L"Uncorrectable Error Mask", 4, 28, L"%d", NULL, NULL, NULL, NULL},
  {L"Uncorrectable Error Severity", 4, 32, L"%d", NULL, NULL, NULL, NULL},
  {L"Correctable Error Mask", 4, 36, L"%d", NULL, NULL, NULL, NULL},
  {L"Advanced Error Capabilities and Control", 4, 40, L"%d", NULL, NULL, NULL,
    NULL},
};

/**
  An ACPI_PARSER array describing the HEST PCI-X Bridge AER structure.
**/
STATIC CONST ACPI_PARSER HestErrorSourcePciExpressBrigdeAerParser[] = {
  PARSE_HEST_ERROR_SOURCE_COMMON_HEADER(),
  {L"Bus", 4, 16, L"%d", NULL, NULL, NULL, NULL},
  {L"Device", 2, 20, L"%d", NULL, NULL, NULL, NULL},
  {L"Function", 2, 22, L"%d", NULL, NULL, NULL, NULL},
  {L"Device Control", 2, 24, L"%d", NULL, NULL, NULL, NULL},
  {L"Reserved", 2, 26, L"%d", NULL, NULL, NULL, NULL},
  {L"Uncorrectable Error Mask", 4, 28, L"%d", NULL, NULL, NULL, NULL},
  {L"Uncorrectable Error Severity", 4, 32, L"%d", NULL, NULL, NULL, NULL},
  {L"Correctable Error Mask", 4, 36, L"%d", NULL, NULL, NULL, NULL},
  {L"Advanced Error Capabilities and Control", 4, 40, L"%d", NULL, NULL, NULL,
    NULL},
  {L"Secondary Uncorrectable Error Mask", 4, 44, L"%d", NULL, NULL, NULL, NULL},
  {L"Secondary Uncorrectable Error Severity", 4, 48, L"%d", NULL, NULL, NULL,
    NULL},
  {L"Secondary Advanced Error Capabilities and Control", 4, 52, L"%d", NULL,
    NULL, NULL, NULL},
};

/**
  An ACPI_PARSER array describing the HEST GHES type structure.
**/
STATIC CONST ACPI_PARSER HestErrorSourceGhesParser[] = {
  PARSE_HEST_GHES_ERROR_SOURCE(),
};

/**
  An ACPI_PARSER array describing the HEST GHESv2 type structure.
**/
STATIC CONST ACPI_PARSER HestErrorSourceGhesv2Parser[] = {
  PARSE_HEST_GHES_ERROR_SOURCE(),
  {L"Read Ack Register", 12, 64, NULL, DumpGas, NULL, NULL, NULL},
  {L"Read Ack Preserve", 8, 76, L"%ld", NULL, NULL, NULL, NULL},
  {L"Read Ack Write", 8, 84, L"%ld", NULL, NULL, NULL, NULL},
};

/**
  This function parses the HEST table.
  When trace is enabled this function parses the HEST table and
  traces the ACPI table fields.

  This function parses the following HEST structures:
  - PCI Express RootPort AER Structure (Type 6)
  - PCI Express Device AER Structure (Type 7)
  - PCI Express Bridge AER Structure (Type 8)
  - Generic Hardware Error Source Structure (Type 9)
  - Generic Hardware Error Source V2 Structure (Type 10)

  This function also performs validation of the ACPI table fields.

  @param [in] Trace              If TRUE, trace the ACPI fields.
  @param [in] Ptr                Pointer to the start of the buffer.
  @param [in] AcpiTableLength    Length of the ACPI table.
  @param [in] AcpiTableRevision  Revision of the ACPI table.
**/
VOID
EFIAPI
ParseAcpiHest (
  IN BOOLEAN Trace,
  IN UINT8*  Ptr,
  IN UINT32  AcpiTableLength,
  IN UINT8   AcpiTableRevision
  )
{
  UINT32 Offset;
  UINT8  *ErrorSourcePtr;

  if (Trace != TRUE) {
    return;
  }

  Offset = ParseAcpi (
             TRUE,
             0,
             "HEST",
             Ptr,
             AcpiTableLength,
             PARSER_PARAMS (HestParser)
             );

  // Validate Error Source Descriptors Count.
  if (HestErrorSourceCount == NULL) {
    IncrementErrorCount ();
    Print (
      L"ERROR: Insufficient length left for Error Source Count.\n"\
      L"       Length left = %d.\n",
      AcpiTableLength - Offset
      );
    return;
  }

  while (Offset < AcpiTableLength) {
    ErrorSourcePtr = Ptr + Offset;

    // Get Type of Error Source Descriptor.
    ParseAcpi (
      FALSE,
      0,
      NULL,
      ErrorSourcePtr,
      AcpiTableLength - Offset,
      PARSER_PARAMS (HestErrorSourceTypeParser)
      );

    // Validate Error Source Descriptors Type.
    if (HestErrorSourceType == NULL) {
      IncrementErrorCount ();
      Print (
        L"ERROR: Insufficient length left for Error Source Type.\n"\
        L"       Length left = %d.\n",
        AcpiTableLength - Offset
        );
      return;
    }

    switch (*HestErrorSourceType) {
      case EFI_ACPI_6_3_PCI_EXPRESS_ROOT_PORT_AER:
        ParseAcpi (
          TRUE,
          2,
          "PCI Express RootPort AER Structure",
          ErrorSourcePtr,
          sizeof (EFI_ACPI_6_3_PCI_EXPRESS_ROOT_PORT_AER_STRUCTURE),
          PARSER_PARAMS (HestErrorSourcePciExpressRootPortAerParser)
          );

        Offset += sizeof (EFI_ACPI_6_3_PCI_EXPRESS_ROOT_PORT_AER_STRUCTURE);
        break;
      case EFI_ACPI_6_3_PCI_EXPRESS_DEVICE_AER:
        ParseAcpi (
          TRUE,
          2,
          "PCI Express Device AER Structure",
          ErrorSourcePtr,
          sizeof (EFI_ACPI_6_3_PCI_EXPRESS_DEVICE_AER_STRUCTURE),
          PARSER_PARAMS (HestErrorSourcePciExpressDeviceAerParser)
          );

        Offset += sizeof (EFI_ACPI_6_3_PCI_EXPRESS_DEVICE_AER_STRUCTURE);
        break;
      case EFI_ACPI_6_3_PCI_EXPRESS_BRIDGE_AER:
        ParseAcpi (
          TRUE,
          2,
          "PCI Express Bridge AER Structure",
          ErrorSourcePtr,
          sizeof (EFI_ACPI_6_3_PCI_EXPRESS_BRIDGE_AER_STRUCTURE),
          PARSER_PARAMS (HestErrorSourcePciExpressBrigdeAerParser)
          );

        Offset += sizeof (EFI_ACPI_6_3_PCI_EXPRESS_BRIDGE_AER_STRUCTURE);
        break;
      case EFI_ACPI_6_3_GENERIC_HARDWARE_ERROR:
        ParseAcpi (
          TRUE,
          2,
          "Generic Hardware Error Source Structure",
          ErrorSourcePtr,
          sizeof (EFI_ACPI_6_3_GENERIC_HARDWARE_ERROR_SOURCE_STRUCTURE),
          PARSER_PARAMS (HestErrorSourceGhesParser)
          );

        Offset += sizeof (EFI_ACPI_6_3_GENERIC_HARDWARE_ERROR_SOURCE_STRUCTURE);
        break;
      case EFI_ACPI_6_3_GENERIC_HARDWARE_ERROR_VERSION_2:
        ParseAcpi (
          TRUE,
          2,
          "Generic Hardware Error Source V2 Structure",
          ErrorSourcePtr,
          sizeof (
            EFI_ACPI_6_3_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE
            ),
          PARSER_PARAMS (HestErrorSourceGhesv2Parser)
          );

        Offset +=
          sizeof (
            EFI_ACPI_6_3_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE
            );
        break;
      default:
        IncrementErrorCount ();
        Print (L"ERROR: Invalid Error Source Descriptor Type.\n");
        return;
    } //switch
  } //while
}
