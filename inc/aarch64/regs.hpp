/*
 * Register File
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

#include "arch.hpp"
#include "compiler.hpp"
#include "hazard.hpp"
#include "types.hpp"

class Space_gst;
class Space_hst;
class Space_obj;
class Space_pio;
class Vmcb;

struct Sys_regs
{
    uintptr_t   gpr[31]     { 0 };
};

static_assert (__is_standard_layout (Sys_regs) && sizeof (Sys_regs) == __SIZEOF_POINTER__ * 31);

struct Exc_regs
{
    Sys_regs    sys;

    struct {
        uint64_t  sp        { 0 };
        uint64_t  tpidr     { 0 };
        uint64_t  tpidrro   { 0 };
    } el0;

    struct {
        uint64_t  elr       { 0 };
        uint64_t  spsr      { SPSR_EL0 };
        uint64_t  esr       { 0 };
        uint64_t  far       { 0 };
    } el2;

    inline auto &ip()       { return el2.elr; }
    inline auto &sp()       { return el0.sp; }

    inline unsigned mode() const { return el2.spsr & SPSR_M; }

    inline auto ep() const { return el2.esr >> 26; }

    inline void set_ep (uint64_t val) { el2.esr = val << 26; }
};

static_assert (__is_standard_layout (Exc_regs) && sizeof (Exc_regs) == __SIZEOF_POINTER__ * 38);

struct alignas (16) Cpu_regs final
{
    Exc_regs            exc;
    Vmcb *        const vmcb;
    Space_obj *   const obj;
    Space_hst *   const hst;
    Space_gst *         gst     { nullptr };
    Hazard              hazard  { 0 };

    inline Cpu_regs (Space_obj *o, Space_hst *h, Space_pio * = nullptr) : vmcb (nullptr), obj (o), hst (h) {}
    inline Cpu_regs (Space_obj *o, Space_hst *h, Vmcb *v) : vmcb (v), obj (o), hst (h), hazard (Hazard::ILLEGAL) {}
};
