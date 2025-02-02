/*
 * Host Page Table (HPT)
 *
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

#include "extern.hpp"
#include "ptab_hpt.hpp"

INIT_PRIORITY (PRIO_PTAB) Hptp Hptp::master { Kmem::ptr_to_phys (&PTAB_HVAS) };

bool Hptp::share_from_master (IAddr v)
{
    auto const d { walk (v, Hpt::lev() - 1, true) };
    if (!d)
        return false;

    auto const s { master.walk (v, Hpt::lev() - 1, false) };
    if (!s)
        return false;

    *d = static_cast<Hpt>(*s);

    return true;
}

void *Hptp::map (OAddr p, bool w)
{
    auto       c { current() };
    auto const s { Hpt::page_size (Hpt::bpl) };
    auto const o { Hpt::offs_mask (Hpt::bpl) & p };

    p &= ~o;

    c.update (MMAP_CPU_TMAP,     p,     Hpt::bpl, Paging::Permissions ((w ? Paging::W : 0) | Paging::R), Memattr::ram());
    c.update (MMAP_CPU_TMAP + s, p + s, Hpt::bpl, Paging::Permissions ((w ? Paging::W : 0) | Paging::R), Memattr::ram());

    invalidate_cpu();

    return reinterpret_cast<void *>(MMAP_CPU_TMAP | o);
}
