/*
 * CoreSight Architecture
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

#include "std.hpp"
#include "types.hpp"

class Coresight
{
    protected:
        enum class Component : unsigned
        {
            CIDR3       =  4,       // Component Identification Register 3
            CIDR2       =  8,       // Component Identification Register 2
            CIDR1       = 12,       // Component Identification Register 1
            CIDR0       = 16,       // Component Identification Register 0
            PIDR3       = 20,       // Peripheral Identification Register 3
            PIDR2       = 24,       // Peripheral Identification Register 2
            PIDR1       = 28,       // Peripheral Identification Register 1
            PIDR0       = 32,       // Peripheral Identification Register 0
        };

        static auto read (Component r, uintptr_t addr) { return *reinterpret_cast<uint32_t volatile *>(addr - std::to_underlying (r)); }
};
