/*
 * Execution Context (EC)
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

#include "assert.hpp"
#include "cpu.hpp"
#include "ec_arch.hpp"
#include "entry.hpp"
#include "event.hpp"
#include "extern.hpp"
#include "fpu.hpp"
#include "pd.hpp"
#include "sc.hpp"
#include "space_gst.hpp"
#include "space_hst.hpp"
#include "stdio.hpp"
#include "timer.hpp"
#include "vmcb.hpp"

// Constructor: Kernel Thread
Ec_arch::Ec_arch (cpu_t c, cont_t x) : Ec (&Space_hst::nova, c, x) {}

// Constructor: HST EC
Ec_arch::Ec_arch (bool t, Fpu *f, Space_obj *obj, Space_hst *hst, Space_pio *pio, cpu_t c, unsigned long e, uintptr_t sp, uintptr_t hva, void *k) : Ec (t, f, obj, hst, pio, k, c, e, t ? send_msg<ret_user_exception> : nullptr)
{
    assert (obj && hst && !pio && k);

    trace (TRACE_CREATE, "EC:%p created (OBJ:%p HST:%p CPU:%u UTCB:%p %c)", static_cast<void *>(this), static_cast<void *>(obj), static_cast<void *>(hst), c, k, subtype == Kobject::Subtype::EC_LOCAL ? 'L' : 'G');

    exc_regs().sp() = sp;
    exc_regs().set_ep (Event::hst_arch + Event::Selector::STARTUP);

    // Map UTCB
    hst->update (hva, Kmem::ptr_to_phys (kpage), 0, Paging::Permissions (Paging::K | Paging::U | Paging::W | Paging::R), Memattr::ram());
}

// Constructor: GST EC
Ec_arch::Ec_arch (bool t, Fpu *f, Space_obj *obj, Space_hst *hst, Vmcb *v, cpu_t c, unsigned long e, uintptr_t sp) : Ec (t, f, obj, hst, v, nullptr, c, e, set_vmm_regs)
{
    assert (obj && hst && v);

    trace (TRACE_CREATE, "EC:%p created (OBJ:%p HST:%p CPU:%u VMCB:%p %c)", static_cast<void *>(this), static_cast<void *>(obj), static_cast<void *>(hst), c, static_cast<void *>(v), subtype == Kobject::Subtype::EC_VCPU_REAL ? 'R' : 'O');

    exc_regs().sp() = sp;
    exc_regs().set_ep (Event::gst_arch + Event::Selector::STARTUP);
}

// Factory: GST EC
Ec *Ec::create_gst (Status &s, Pd *pd, bool t, bool fpu, cpu_t cpu, unsigned long evt, uintptr_t sp, uintptr_t /*hva*/)
{
    auto const obj { pd->get_obj() };
    auto const hst { pd->get_hst() };

    if (EXPECT_FALSE (!obj || !hst)) {
        s = Status::ABORTED;
        return nullptr;
    }

    // FIXME: Refcount updates

    auto const f { fpu ? new (pd->fpu_cache) Fpu : nullptr };
    auto const v { new Vmcb };
    Ec *ec;

    if (EXPECT_TRUE ((!fpu || f) && v && (ec = new (cache) Ec_arch (t, f, obj, hst, v, cpu, evt, sp))))
        return ec;

    delete v;
    Fpu::operator delete (f, pd->fpu_cache);

    s = Status::MEM_OBJ;

    return nullptr;
}

void Ec::adjust_offset_ticks (uint64_t t)
{
    if (subtype == Kobject::Subtype::EC_VCPU_OFFS)
        regs.vmcb->tmr.cntvoff += t;
}

void Ec::handle_hazard (unsigned h, cont_t func)
{
    if (EXPECT_FALSE (h & (Hazard::ILLEGAL | Hazard::RECALL | Hazard::SLEEP | Hazard::SCHED))) {

        Cpu::preemption_point();

        if (Cpu::hazard & Hazard::SLEEP) {      // Reload
            cont = func;
            Cpu::fini();
        }

        if (Cpu::hazard & Hazard::SCHED) {      // Reload
            cont = func;
            Scheduler::schedule();
        }

        if (h & Hazard::ILLEGAL)
            kill ("Illegal execution state");

        if (regs.hazard & Hazard::RECALL) {     // Reload

            regs.hazard.clr (Hazard::RECALL);

            if (func == Ec_arch::ret_user_vmexit) {
                exc_regs().set_ep (Event::gst_arch + Event::Selector::RECALL);
                send_msg<Ec_arch::ret_user_vmexit> (this);
            } else {
                exc_regs().set_ep (Event::hst_arch + Event::Selector::RECALL);
                send_msg<Ec_arch::ret_user_exception> (this);
            }
        }
    }

    // Point of no return after checking all diversions: this EC will run

    if (EXPECT_FALSE (h & Hazard::FPU))
        Cpu::hazard & Hazard::FPU ? Fpu::disable() : Fpu::enable();

    if (EXPECT_FALSE (h & Hazard::BOOT_HST)) {
        Cpu::hazard &= ~Hazard::BOOT_HST;
        trace (TRACE_PERF, "TIME: First HEC: %lums", Stc::ticks_to_ms (Timer::time() - *reinterpret_cast<uintptr_t *>(Kmem::sym_to_virt (&__boot_ts))));
    }

    if (EXPECT_FALSE (h & Hazard::BOOT_GST)) {
        Cpu::hazard &= ~Hazard::BOOT_GST;
        trace (TRACE_PERF, "TIME: First GEC: %lums", Stc::ticks_to_ms (Timer::time() - *reinterpret_cast<uintptr_t *>(Kmem::sym_to_virt (&__boot_ts))));
    }
}

void Ec_arch::ret_user_hypercall (Ec *const self)
{
    auto const h { (Cpu::hazard ^ self->regs.hazard) & (Hazard::ILLEGAL | Hazard::RECALL | Hazard::FPU | Hazard::BOOT_HST | Hazard::RCU | Hazard::SLEEP | Hazard::SCHED) };
    if (EXPECT_FALSE (h))
        self->handle_hazard (h, ret_user_hypercall);

    trace (TRACE_CONT, "EC:%p %s to M:%#x IP:%#lx SP:%#lx", static_cast<void *>(self), __func__, self->exc_regs().mode(), self->exc_regs().ip(), self->exc_regs().sp());

    if (Vmcb::current)
        Vmcb::load_hst();

    self->get_hst()->make_current();

    asm volatile ("mov sp, %0;" EXPAND (LOAD_STATE ERET) : : "r" (&self->exc_regs()), "m" (self->exc_regs()));

    UNREACHED;
}

void Ec_arch::ret_user_exception (Ec *const self)
{
    auto const h { (Cpu::hazard ^ self->regs.hazard) & (Hazard::ILLEGAL | Hazard::RECALL | Hazard::FPU | Hazard::BOOT_HST | Hazard::RCU | Hazard::SLEEP | Hazard::SCHED) };
    if (EXPECT_FALSE (h))
        self->handle_hazard (h, ret_user_exception);

    trace (TRACE_CONT, "EC:%p %s to M:%#x IP:%#lx SP:%#lx", static_cast<void *>(self), __func__, self->exc_regs().mode(), self->exc_regs().ip(), self->exc_regs().sp());

    if (Vmcb::current)
        Vmcb::load_hst();

    self->get_hst()->make_current();

    asm volatile ("mov sp, %0;" EXPAND (LOAD_STATE ERET) : : "r" (&self->exc_regs()), "m" (self->exc_regs()));

    UNREACHED;
}

void Ec_arch::ret_user_vmexit (Ec *const self)
{
    auto const h { (Cpu::hazard ^ self->regs.hazard) & (Hazard::ILLEGAL | Hazard::RECALL | Hazard::FPU | Hazard::BOOT_GST | Hazard::RCU | Hazard::SLEEP | Hazard::SCHED) };
    if (EXPECT_FALSE (h))
        self->handle_hazard (h, ret_user_vmexit);

    trace (TRACE_CONT, "EC:%p %s to M:%#x IP:%#lx", static_cast<void *>(self), __func__, self->exc_regs().mode(), self->exc_regs().ip());

    auto const v { self->regs.vmcb };

    if (Vmcb::current != v)
        v->load_gst();      // Restore full register state
    else
        v->load_tmr();      // Restore only vTMR PPI state

    self->get_gst()->make_current();

    asm volatile ("mov sp, %0;" EXPAND (LOAD_STATE ERET) : : "r" (&self->exc_regs()), "m" (self->exc_regs()));

    UNREACHED;
}

void Ec_arch::set_vmm_regs (Ec *const self)
{
    assert (self->is_vcpu());
    assert (self->cpu == Cpu::id);

    auto const v { self->regs.vmcb };

    Cpu::set_vmm_regs (self->sys_regs().gpr, v->el2.hcr, v->el2.vpidr, v->el2.vmpidr, v->gic.elrsr);

    send_msg<ret_user_vmexit> (self);
}
