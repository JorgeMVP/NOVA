/*
 * Read-Copy Update (RCU)
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

#include "barrier.hpp"
#include "counter.hpp"
#include "cpu.hpp"
#include "hazard.hpp"
#include "initprio.hpp"
#include "rcu.hpp"
#include "stdio.hpp"

uintptr_t   Rcu::state = RCU_CMP;
uintptr_t   Rcu::count;
uintptr_t   Rcu::l_batch;
uintptr_t   Rcu::c_batch;

INIT_PRIORITY (PRIO_LOCAL) Rcu_list Rcu::next;
INIT_PRIORITY (PRIO_LOCAL) Rcu_list Rcu::curr;
INIT_PRIORITY (PRIO_LOCAL) Rcu_list Rcu::done;

void Rcu::invoke_batch()
{
    for (Rcu_elem *e = done.head, *n; e; e = n) {
        n = e->next;
        (e->func)(e);
    }

    done.clear();
}

void Rcu::start_batch (State s)
{
    uintptr_t v, m = RCU_CMP | RCU_PND;

    do if ((v = state) >> 2 != l_batch) return; while (!(v & s) && !__atomic_compare_exchange_n (&state, &v, v | s, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));

    if ((v ^ ~s) & m)
        return;

    count = Cpu::count;

    barrier();

    state++;
}

void Rcu::quiet()
{
    Cpu::hazard &= ~Hazard::RCU;

    if (__atomic_sub_fetch (&count, 1, __ATOMIC_SEQ_CST) == 0)
        start_batch (RCU_CMP);
}

void Rcu::update()
{
    if (l_batch != batch()) {
        l_batch = batch();
        Cpu::hazard |= Hazard::RCU;
    }

    if (curr.head && complete (c_batch))
        done.append (&curr);

    if (!curr.head && next.head) {
        curr.append (&next);

        c_batch = l_batch + 1;

        start_batch (RCU_PND);
    }

    if (done.head)
        invoke_batch();
}
