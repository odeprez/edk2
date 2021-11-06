/** @file

  Copyright (c) 2016-2021, Arm Limited. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef MM_COMMUNICATE_H_
#define MM_COMMUNICATE_H_

#define MM_MAJOR_VER_MASK   0xEFFF0000
#define MM_MINOR_VER_MASK   0x0000FFFF
#define MM_MAJOR_VER_SHIFT  16

#define MM_MAJOR_VER(x)  (((x) & MM_MAJOR_VER_MASK) >> MM_MAJOR_VER_SHIFT)
#define MM_MINOR_VER(x)  ((x) & MM_MINOR_VER_MASK)

#if (FixedPcdGet32 (PcdFfaEnable) == 1)
#define MM_CALLER_MAJOR_VER  0x1UL
#define MM_CALLER_MINOR_VER  0x1UL
#else
#define MM_CALLER_MAJOR_VER  0x1UL
#define MM_CALLER_MINOR_VER  0x0UL
#endif

#endif /* MM_COMMUNICATE_H_ */
