/* BFD support for the RB GTM MCS architecture.
   Copyright 2011 Free Software Foundation, Inc.
   Contributed by Horst Lehser <Horst.Lehser@hightec-rt.com>

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "elf/tricore.h"

/* we can link MCS modules into Tricore and PowerPC application */
static const bfd_arch_info_type *
mcs_compatible (const bfd_arch_info_type *a, const bfd_arch_info_type *b)
{
  BFD_ASSERT(a->arch == bfd_arch_mcs);
  switch (b->arch)
    {
    default:
      return NULL;
    case bfd_arch_mcs:
      return bfd_default_compatible(a,b);
    case bfd_arch_tricore:
        return b;
    case bfd_arch_powerpc:
        return b;
    }
}

const bfd_arch_info_type bfd_mcs_arch = 
{
    32,                         /* Bits in word.  */
    32,                         /* Bits in address.  */
    8,                          /* Bits in byte.  */
    bfd_arch_mcs,               /* Enum bfd_architecture.  */ 
    bfd_mach_mcs,               /* Machine number.  */
    "mcs",                      /* Architecture name.  */
    "GTM-MCS",                  /* Printable name.  */
    4,                          /* Alignment.  */
    true,                       /* Is this the default machine for the target.  */
    mcs_compatible,             /* Function callback to test if two files have compatible machines.  */
    bfd_default_scan,           /* Let BFD find the default arch.  */
    bfd_arch_default_fill,      /* Default fill.  */
    NULL,                       /* Next.  */
    0                           /* Maximum offset of a reloc from the start of an insn. */	    
};

