/*
 * Global Descriptor Table (GDT)
 *
 * Copyright (C) 2009-2011 Udo Steinberg <udo@hypervisor.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2012-2013 Udo Steinberg, Intel Corporation.
 * Copyright (C) 2019-2023 Udo Steinberg, BedRock Systems, Inc.
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

#include "descriptor.hpp"
#include "selectors.hpp"

class Gdt final : private Descriptor
{
    private:
        uint32_t val[2];

        ALWAYS_INLINE
        inline void set32 (Type type, Granularity gran, Size size, bool l, unsigned dpl, uintptr_t base, uintptr_t limit)
        {
            val[0] = static_cast<uint32_t>(base << 16 | (limit & 0xffff));
            val[1] = static_cast<uint32_t>((base & 0xff000000) | gran | size | (limit & 0xf0000) | l << 21 | 1u << 15 | dpl << 13 | type | (base >> 16 & 0xff));
        }

        ALWAYS_INLINE
        inline void set64 (Type type, Granularity gran, Size size, bool l, unsigned dpl, uintptr_t base, uintptr_t limit)
        {
            set32 (type, gran, size, l, dpl, base, limit);
            (this + 1)->val[0] = static_cast<uint32_t>(base >> 32);
            (this + 1)->val[1] = 0;
        }

    public:
        static Gdt gdt[SEL_MAX >> 3] CPULOCAL;

        static void build();

        ALWAYS_INLINE
        static inline void load()
        {
            Pseudo_descriptor d { gdt, sizeof (gdt) };
            asm volatile ("lgdt %0" : : "m" (d));
        }

        ALWAYS_INLINE
        static inline void unbusy_tss()
        {
            gdt[SEL_TSS_RUN >> 3].val[1] &= ~0x200;
        }
};
