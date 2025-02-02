/*
 * Host Memory Space
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

#pragma once

#include "ptab_npt.hpp"
#include "space_mem.hpp"

class Space_hst final : public Space_mem<Space_hst>
{
    private:
        Vmid const  vmid;
        Nptp        nptp;

        Space_hst();

        inline Space_hst (Pd *p) : Space_mem (Kobject::Subtype::HST, p) {}

    public:
        static Space_hst nova;

        static inline auto selectors() { return BIT64 (Npt::ibits - PAGE_BITS); }
        static inline auto max_order() { return Npt::lev_ord(); }

        [[nodiscard]] static inline Space_hst *create (Status &s, Slab_cache &cache, Pd *pd)
        {
            auto const hst { new (cache) Space_hst (pd) };

            if (EXPECT_TRUE (hst)) {

                if (EXPECT_TRUE (hst->nptp.root_init()))
                    return hst;

                operator delete (hst, cache);
            }

            s = Status::MEM_OBJ;

            return nullptr;
        }

        inline void destroy (Slab_cache &cache) { operator delete (this, cache); }

        inline auto lookup (uint64_t v, uint64_t &p, unsigned &o, Memattr &ma) const { return nptp.lookup (v, p, o, ma); }

        inline auto update (uint64_t v, uint64_t p, unsigned o, Paging::Permissions pm, Memattr ma) { return nptp.update (v, p, o, pm, ma); }

        inline void sync() { nptp.invalidate (vmid); }

        inline void make_current() { nptp.make_current (vmid); }

        static void user_access (uint64_t addr, size_t size, bool a) { Space_mem::user_access (nova, addr, size, a, Memattr::dev()); }
};
