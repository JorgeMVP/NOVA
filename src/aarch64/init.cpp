/*
 * Initialization Code
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

#include "acpi.hpp"
#include "cmdline.hpp"
#include "console.hpp"
#include "cpu.hpp"
#include "extern.hpp"
#include "fdt.hpp"
#include "kmem.hpp"
#include "ptab_hpt.hpp"
#include "smmu.hpp"

extern "C"
Hpt::OAddr kern_ptab_setup (unsigned cpu)
{
    if (Acpi::resume)
        return Cpu::remote_ptab (cpu);

    Hptp hptp; uintptr_t phys;

    // Share kernel code and data
    hptp.share_from_master (LINK_ADDR);

    // Allocate and map kernel stack
    hptp.update (MMAP_CPU_DSTK, Kmem::ptr_to_phys (Buddy::alloc (0, Buddy::Fill::BITS0)), 0,
                 Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::ram());

    // Allocate and map cpu page
    hptp.update (MMAP_CPU_DATA, phys = Kmem::ptr_to_phys (Buddy::alloc (0, Buddy::Fill::BITS0)), 0,
                 Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::ram());

    // Add to CPU array
    Hptp::master_map (MMAP_GLB_DATA + cpu * PAGE_SIZE, phys, 0,
                      Paging::Permissions (Paging::G | Paging::W | Paging::R), Memattr::ram());

    return hptp.root_addr();
}

extern "C"
unsigned init (uintptr_t offset)
{
    if (Acpi::resume) {

        // Restart all cores
        for (cpu_t c { 0 }; c < Cpu::count; c++)
            Psci::boot_cpu (c, Cpu::remote_mpidr (c));

    } else {

        Kmem::init (offset);

        Buddy::init();

        for (void (**func)() = &CTORS_S; func != &CTORS_E; (*func++)()) ;

        Cmdline::init();

        for (void (**func)() = &CTORS_C; func != &CTORS_S; (*func++)()) ;

        // Now we're ready to talk to the world
        Console::print ("\nNOVA Microhypervisor #%07lx (%s): %s %s [%s]\n", reinterpret_cast<uintptr_t>(&GIT_VER), ARCH, __DATE__, __TIME__, COMPILER_STRING);
    }

    Acpi::init() || Fdt::init();

    // If SMMUs were not enumerated by firmware, then enumerate them based on board knowledge
    if (!Smmu::avail_smg() && !Smmu::avail_ctx())
        for (unsigned i = 0; i < sizeof (Board::smmu) / sizeof (*Board::smmu); i++)
            if (Board::smmu[i].mmio)
                new Smmu (Board::smmu[i]);

    return Cpu::boot_cpu;
}
