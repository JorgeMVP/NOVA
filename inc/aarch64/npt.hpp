/*
 * Nested Page Table (NPT)
 *
 * Copyright (C) 2019-2021 Udo Steinberg, BedRock Systems, Inc.
 *
 * This file is part of the NOVA microhypervisor.
 *
 * NOVA is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NOVA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 */

#pragma once

#include "assert.hpp"
#include "memattr.hpp"
#include "pte.hpp"
#include "vmid.hpp"

class Npt : public Pte<Npt, 3, 9, uint64, uint64>
{
    friend class Pte;

    using Pte::Pte;

    public:
        static inline bool xnx { false };

    protected:
        static constexpr OAddr ADDR_MASK = BIT64_RANGE (47, 12);

    private:
        enum
        {
            ATTR_P      = BIT64  (0),   // Present
            ATTR_nL     = BIT64  (1),   // Not Large
            ATTR_R      = BIT64  (6),   // Readable
            ATTR_W      = BIT64  (7),   // Writable
            ATTR_A      = BIT64 (10),   // Accessed
            ATTR_nX0    = BIT64 (53),   // Not Executable
            ATTR_nX1    = BIT64 (54),   // Not Executable
            ATTR_K      = BIT64 (55),   // Kernel Memory
        };

        bool large (unsigned l) const { return              l && (val & (ATTR_nL | ATTR_P)) == (ATTR_P); }
        bool table (unsigned l) const { return l == lev || (l && (val & (ATTR_nL | ATTR_P)) == (ATTR_nL | ATTR_P)); }

        // Attributes for PTEs referring to page tables
        static OAddr ptab_attr()
        {
            return ATTR_nL | ATTR_P;
        }

        // Attributes for PTEs referring to leaf pages
        static OAddr page_attr (unsigned l, Paging::Permissions pm, Memattr::Cacheability ca, Memattr::Shareability sh)
        {
            bool nxs = !(pm & Paging::XS);
            bool nxu = !(pm & Paging::XU);

            return !(pm & Paging::API) ? 0 :
                     ATTR_K   * !!(pm & Paging::K)          |
                     ATTR_nX1 * ((nxs & nxu) | (xnx & nxu)) |
                     ATTR_nX0 * ((nxs ^ nxu) &  xnx)        |
                     ATTR_W   * !!(pm & Paging::W)          |
                     ATTR_R   * !!(pm & Paging::R)          |
                    (static_cast<OAddr>(sh) & 0x3) << 8 | (Memattr::npt_attr (ca) & 0xf) << 2 | !l * ATTR_nL | ATTR_A | ATTR_P;
        }

        static Paging::Permissions page_pm (OAddr a)
        {
            return Paging::Permissions (!a ? 0 :
                                      !!(a & ATTR_K)                      * Paging::K  |
                                     !(!(a & ATTR_nX1) ^ !(a & ATTR_nX0)) * Paging::XS |
                                       !(a & ATTR_nX1)                    * Paging::XU |
                                      !!(a & ATTR_W)                      * Paging::W  |
                                      !!(a & ATTR_R)                      * Paging::R);
        }

        static Memattr::Cacheability page_ca (OAddr a, unsigned) { return Memattr::Cacheability ((a & 0x30 ? 4 : 0) + (a >> 2 & 0x3)); }

        static Memattr::Shareability page_sh (OAddr a) { return Memattr::Shareability (a >> 8 & 0x3); }
};

class Nptp final : public Npt
{
    private:
        static uint64 current CPULOCAL;

    public:
        ALWAYS_INLINE
        inline void make_current (Vmid vmid)
        {
            assert (!(val & ~ADDR_MASK));

            uint64 vttbr = static_cast<uint64>(vmid) << 48 | val;

            if (current != vttbr)
                asm volatile ("msr vttbr_el2, %0; isb" : : "r" (current = vttbr) : "memory");
        }

        ALWAYS_INLINE
        inline void flush (Vmid vmid)
        {
            make_current (vmid);

            asm volatile ("dsb  ishst           ;"  // Ensure PTE writes have completed
                          "tlbi vmalls12e1is    ;"  // Invalidate TLB
                          "dsb  ish             ;"  // Ensure TLB invalidation completed
                          "isb                  ;"  // Ensure subsequent instructions use new translation
                          : : : "memory");
        }
};
