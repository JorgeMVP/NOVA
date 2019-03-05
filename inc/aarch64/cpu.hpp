/*
 * Central Processing Unit (CPU)
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

#include "arch.hpp"
#include "atomic.hpp"
#include "compiler.hpp"
#include "kmem.hpp"
#include "spinlock.hpp"
#include "types.hpp"

class Cpu
{
    private:
        static constexpr auto hyp0_cptr =   0;

        static constexpr auto hyp1_cptr =   CPTR_TAM        |   // Trap activity monitor: AMCFGR, AMCGCR, AMCNTEN*, AMCR, AMEVCNTR*, AMEVTYPER*, AMUSERENR
                                            CPTR_TTA        |   // Trap trace registers
                                            CPTR_TZ;            // Trap ZCR

        static constexpr auto hyp0_hcr  =   HCR_ATA         |   // Trap GCR, RGSR, TFSR*
                                            HCR_ENSCXT      |   // Trap SCXTNUM
                                            HCR_FIEN        |   // Trap ERXPFG*
                                            HCR_NV2         |
                                            HCR_NV1         |
                                            HCR_NV          |
                                            HCR_APK         |   // Trap APDAKey*, APDBKey*, APGAKey*, APIAKey*, APIBKey*
                                            HCR_E2H         |
                                            HCR_ID          |
                                            HCR_CD          |
                                            HCR_TGE         |
                                            HCR_DC;

        static constexpr auto hyp1_hcr =    HCR_TID5        |   // Trap GMID
                                            HCR_TERR        |   // Trap ERRIDR, ERRSELR, ERXADDR, ERXCTLR, ERXFR, ERXMISC*, ERXSTATUS
                                            HCR_TLOR        |   // Trap LORC, LOREA, LORID, LORN, LORSA
                                            HCR_TSW         |   // Trap DC ISW/CSW/CISW
                                            HCR_TACR        |   // Trap ACTLR
                                            HCR_TIDCP       |   // Trap S3_* implementation defined registers
                                            HCR_TSC         |   // Trap SMC
                                            HCR_TID3        |   // Trap ID_AA64AFR*,ID_AA64DFR*, ID_AA64ISAR*, ID_AA64MMFR*, ID_AA64PFR*, ID_AA64ZFR*, ID_AFR0, ID_DFR0, ID_ISAR*, ID_MMFR, ID_PFR, MVFR*
                                            HCR_TID1        |   // Trap AIDR, REVIDR
                                            HCR_TID0        |   // Trap JIDR
                                            HCR_TWE         |   // Trap WFE
                                            HCR_TWI         |   // Trap WFI
                                            HCR_BSU_INNER   |
                                            HCR_FB          |
                                            HCR_AMO         |
                                            HCR_IMO         |
                                            HCR_FMO         |
                                            HCR_PTW         |
                                            HCR_SWIO        |
                                            HCR_VM;

        static constexpr auto hyp0_mdcr =   MDCR_E2TB       |   // Trap trace buffer controls: TRB*
                                            MDCR_E2PB;          // Trap profiling buffer control: PMB*

        static constexpr auto hyp1_mdcr =   MDCR_TDCC       |   // Trap debug comms channel: OSDTRRX, OSDTRTX, MDCCSR, MDCCINT, DBGDTR, DBGDTRRX, DBGDTRTX
                                            MDCR_TTRF       |   // Trap trace filter: TRFCR
                                            MDCR_TPMS       |   // Trap performance monitor sampling: PMS*
                                            MDCR_TDE        |   // Trap all of TDRA+TDOSA+TDA
                                            MDCR_TPM;           // Trap performance monitor access: PMCR, PM*

        static uint64   midr                CPULOCAL;           // Main ID Register
        static uint64   mpidr               CPULOCAL;           // Multiprocessor Affinity Register

        static uint64   res0_hcr            CPULOCAL;           // RES0 bits in HCR
        static uint64   res0_sctlr32        CPULOCAL;           // RES0 bits in SCTLR (32bit mode)
        static uint64   res1_sctlr32        CPULOCAL;           // RES1 bits in SCTLR (32bit mode)
        static uint64   res0_sctlr64        CPULOCAL;           // RES0 bits in SCTLR (64bit mode)
        static uint64   res1_sctlr64        CPULOCAL;           // RES1 bits in SCTLR (64bit mode)
        static uint64   res0_spsr32         CPULOCAL;           // RES0 bits in SPSR  (32bit mode)
        static uint64   res0_spsr64         CPULOCAL;           // RES0 bits in SPSR  (64bit mode)
        static uint64   res0_tcr32          CPULOCAL;           // RES0 bits in TCR   (32bit mode)
        static uint64   res0_tcr64          CPULOCAL;           // RES0 bits in TCR   (64bit mode)

        static uint64   feat_cpu64[2]       CPULOCAL;           // ID_AA64PFRx
        static uint64   feat_dbg64[2]       CPULOCAL;           // ID_AA64DFRx
        static uint64   feat_isa64[3]       CPULOCAL;           // ID_AA64ISARx
        static uint64   feat_mem64[3]       CPULOCAL;           // ID_AA64MMFRx
        static uint64   feat_sve64[1]       CPULOCAL;           // ID_AA64ZFRx

        static uint32   feat_cpu32[3]       CPULOCAL;           // ID_PFRx
        static uint32   feat_dbg32[2]       CPULOCAL;           // ID_DFRx
        static uint32   feat_isa32[7]       CPULOCAL;           // ID_ISARx
        static uint32   feat_mem32[6]       CPULOCAL;           // ID_MMFRx
        static uint32   feat_mfp32[3]       CPULOCAL;           // MVFRx

        static inline Spinlock boot_lock    asm ("__boot_lock");

        static void enumerate_features();

    public:
        enum class Cpu_feature  // Appendix K13
        {
            EL0         =  0,   // EL0 Support AArch64 only (1), AArch64+AArch32 (2)
            EL1         =  1,   // EL1 Support AArch64 only (1), AArch64+AArch32 (2)
            EL2         =  2,   // EL2 Support AArch64 only (1), AArch64+AArch32 (2)
            EL3         =  3,   // EL3 Support AArch64 only (1), AArch64+AArch32 (2)
            FP          =  4,   // Floating Point (0), FEAT_FP16 (1), Unsupported (15)
            ADVSIMD     =  5,   // Advanced SIMD (0), FEAT_FP16 (1), Unsupported (15)
            GIC         =  6,   // GIC System Registers v3.0/4.0 (1), v4.1 (3)
            RAS         =  7,   // FEAT_RASv1 (1), FEAT_RASv1p1 (2)
            SVE         =  8,   // FEAT_SVE (1)
            SEL2        =  9,   // FEAT_SEL2 (1)
            MPAM        = 10,   // FEAT_MPAM (1)
            AMU         = 11,   // FEAT_AMUv1 (1), FEAT_AMUv1p1 (2)
            DIT         = 12,   // FEAT_DIT (1)
            RME         = 13,   // FEAT_RME (1)
            CSV2        = 14,   // FEAT_CSV2 (1,2)
            CSV3        = 15,   // FEAT_CSV3 (1)
            BT          = 16,   // FEAT_BTI (1)
            SSBS        = 17,   // FEAT_SSBS (1,2)
            MTE         = 18,   // FEAT_MTE (1), FEAT_MTE2 (2), FEAT_MTE3 (3)
            RAS_FRAC    = 19,   // FEAT_RASv1p1 (1)
            MPAM_FRAC   = 20,   // FEAT_MPAM_frac (1)
            RNDR_TRAP   = 23,   // FEAT_RNG_TRAP (1)
            CSV2_FRAC   = 24,   // FEAT_CSV2_1p1 (1), FEAT_CSV2_1p2 (2)
        };

        enum class Dbg_feature  // Appendix K13
        {
            DEBUGVER    =  0,   // FEAT_Debugv8p0 (6), FEAT_Debugv8p1 (7), FEAT_Debugv8p2 (8), FEAT_Debugv8p4 (9)
            TRACEVER    =  1,   // Trace Support
            PMUVER      =  2,   // FEAT_PMUv3 (1), FEAT_PMUv3p1 (4), FEAT_PMUv3p4 (5), FEAT_PMUv3p5 (6), FEAT_PMUv3p7 (7)
            BRPS        =  3,   // Number of Breakpoints - 1
            WRPS        =  5,   // Number of Watchpoints - 1
            CTXCMPS     =  7,   // Number of Context-Aware Breakpoints - 1
            PMSVER      =  8,   // FEAT_SPE (1), FEAT_SPEv1p1 (2), FEAT_SPEv1p2 (3)
            DOUBLELOCK  =  9,   // FEAT_DoubleLock (0), Unsupported (15)
            TRACEFILT   = 10,   // FEAT_TRF (1)
            TRACEBUFFER = 11,   // FEAT_TRBE (1)
            MTPMU       = 12,   // FEAT_MTPMU (1)
            BRBE        = 13,   // FEAT_BRBE (1)
            CSRE        = 14,   // FEAT_CSRE (1)
        };

        enum class Isa_feature  // Appendix K13
        {
            AES         =  1,   // FEAT_AES (1), FEAT_PMULL (2)
            SHA1        =  2,   // FEAT_SHA1 (1)
            SHA2        =  3,   // FEAT_SHA256 (1), FEAT_SHA512 (2)
            CRC32       =  4,   // FEAT_CRC (1)
            ATOMIC      =  5,   // FEAT_LSE (2)
            TME         =  6,   // FEAT_TME (1)
            RDM         =  7,   // FEAT_RDM (1)
            SHA3        =  8,   // FEAT_SHA3 (1)
            SM3         =  9,   // FEAT_SM3 (1)
            SM4         = 10,   // FEAT_SM4 (1)
            DP          = 11,   // FEAT_DotProd (1)
            FHM         = 12,   // FEAT_FHM (1)
            TS          = 13,   // FEAT_FlagM (1), FEAT_FlagM2 (2)
            TLB         = 14,   // FEAT_TLBIOS (1,2), FEAT_TLBIRANGE (2)
            RNDR        = 15,   // FEAT_RNG (1)
            DPB         = 16,   // FEAT_DPB (1), FEAT_DPB2 (2)
            APA         = 17,   // FEAT_PAuth (0,1,2), FEAT_PAuth2 (3), FEAT_FPAC (4,5)
            API         = 18,   // FEAT_PAuth (0,1,2), FEAT_PAuth2 (3), FEAT_FPAC (4,5)
            JSCVT       = 19,   // FEAT_JSCVT (1)
            FCMA        = 20,   // FEAT_FCMA (1)
            LRCPC       = 21,   // FEAT_LRCPC (1), FEAT_LRCPC2 (2)
            GPA         = 22,   // FEAT_PAuth
            GPI         = 23,   // FEAT_PAuth
            FRINTTS     = 24,   // FEAT_FRINTTS (1)
            SB          = 25,   // FEAT_SB (1)
            SPECRES     = 26,   // FEAT_SPECRES (1)
            BF16        = 27,   // FEAT_BF16 (1)
            DGH         = 28,   // FEAT_DGH (1)
            I8MM        = 29,   // FEAT_I8MM (1)
            XS          = 30,   // FEAT_XS (1)
            LS64        = 31,   // FEAT_LS64 (1), FEAT_LS64_V (2), FEAT_LS64_ACCDATA (3)
            WFXT        = 32,   // FEAT_WFxT (1)
            RPRES       = 33,   // FEAT_RPRES (1)
        };

        enum class Mem_feature  // Appendix K13
        {
            PARANGE     =  0,   // FEAT_LPA (6)
            ASIDBITS    =  1,   // Number of ASID Bits
            BIGEND      =  2,   // Mixed Endian Support
            SNSMEM      =  3,   // Secure/Non-Secure Memory Distinction
            BIGEND_EL0  =  4,   // Mixed Endian Support at EL0
            TGRAN16     =  5,   // Stage-1 Granule 16KB
            TGRAN64     =  6,   // Stage-1 Granule 64KB
            TGRAN4      =  7,   // Stage-1 Granule  4KB
            TGRAN16_2   =  8,   // FEAT_GTG Stage-2 Granule 16KB
            TGRAN64_2   =  9,   // FEAT_GTG Stage-2 Granule 64KB
            TGRAN4_2    = 10,   // FEAT_GTG Stage-2 Granule  4KB
            EXS         = 11,   // FEAT_ExS (1)
            FGT         = 14,   // FEAT_FGT (1)
            ECV         = 15,   // FEAT_ECV (1,2)
            HAFDBS      = 16,   // FEAT_HAFDBS (1,2)
            VMIDBITS    = 17,   // FEAT_VMID16 (2)
            VH          = 18,   // FEAT_VHE (1)
            HPDS        = 19,   // FEAT_HPDS (1), FEAT_HPDS2 (2)
            LO          = 20,   // FEAT_LOR (1)
            PAN         = 21,   // FEAT_PAN (1), FEAT_PAN2 (2), FEAT_PAN3 (3)
            SPECSEI     = 22,   // RAS SError on speculative reads/fetches
            XNX         = 23,   // FEAT_XNX (1)
            TWED        = 24,   // FEAT_TWED (1)
            ETS         = 25,   // FEAT_ETS (1)
            HCX         = 26,   // FEAT_HCX (1)
            AFP         = 27,   // FEAT_AFP (1)
            NTLBPA      = 28,   // FEAT_nTLBPA (1)
            CNP         = 32,   // FEAT_TTCNP (1)
            UAO         = 33,   // FEAT_UAO (1)
            LSM         = 34,   // FEAT_LSMAOC (1)
            IESB        = 35,   // FEAT_IESB (1)
            VARANGE     = 36,   // FEAT_LVA (1)
            CCIDX       = 37,   // FEAT_CCIDX (1)
            NV          = 38,   // FEAT_NV (1), FEAT_NV2 (2)
            ST          = 39,   // FEAT_TTST (1)
            AT          = 40,   // FEAT_LSE2 (1)
            IDS         = 41,   // FEAT_IDST (1)
            FWB         = 42,   // FEAT_S2FWB (1)
            TTL         = 44,   // FEAT_TTL (1)
            BBM         = 45,   // FEAT_BBM (0,1,2)
            EVT         = 46,   // FEAT_EVT (1,2)
            E0PD        = 47,   // FEAT_E0PD (1)
        };

        static unsigned         id          CPULOCAL;
        static unsigned         hazard      CPULOCAL;
        static bool             bsp         CPULOCAL;
        static Atomic<uint32>   affinity    CPULOCAL;
        static uint64           cptr        CPULOCAL;
        static uint64           mdcr        CPULOCAL;

        static inline Atomic<unsigned>      online { 0 };

        static inline unsigned  boot_cpu    { 0 };
        static inline unsigned  count       { 0 };

        ALWAYS_INLINE
        static inline uint32 remote_affinity (unsigned cpu) { return *Kmem::loc_to_glob (&affinity, cpu); }

        ALWAYS_INLINE
        static inline void preemption_disable() { asm volatile ("msr daifset, #0xf" : : : "memory"); }

        ALWAYS_INLINE
        static inline void preemption_enable() { asm volatile ("msr daifclr, #0xf" : : : "memory"); }

        ALWAYS_INLINE
        static inline void preemption_point() { asm volatile ("msr daifclr, #0xf; msr daifset, #0xf" : : : "memory"); }

        ALWAYS_INLINE
        static inline void halt() { asm volatile ("wfi; msr daifclr, #0xf; msr daifset, #0xf" : : : "memory"); }

        static inline unsigned feature (Cpu_feature f) { return feat_cpu64[static_cast<unsigned>(f) / 16] >> static_cast<unsigned>(f) % 16 * 4 & BIT_RANGE (3, 0); }
        static inline unsigned feature (Dbg_feature f) { return feat_dbg64[static_cast<unsigned>(f) / 16] >> static_cast<unsigned>(f) % 16 * 4 & BIT_RANGE (3, 0); }
        static inline unsigned feature (Isa_feature f) { return feat_isa64[static_cast<unsigned>(f) / 16] >> static_cast<unsigned>(f) % 16 * 4 & BIT_RANGE (3, 0); }
        static inline unsigned feature (Mem_feature f) { return feat_mem64[static_cast<unsigned>(f) / 16] >> static_cast<unsigned>(f) % 16 * 4 & BIT_RANGE (3, 0); }

        static inline auto constrain_hcr     (uint64 v) { return (v | hyp1_hcr)     & ~(hyp0_hcr | res0_hcr); }
        static inline auto constrain_sctlr32 (uint64 v) { return (v | res1_sctlr32) & ~res0_sctlr32; }
        static inline auto constrain_sctlr64 (uint64 v) { return (v | res1_sctlr64) & ~res0_sctlr64; }
        static inline auto constrain_spsr32  (uint64 v) { return  v                 & ~res0_spsr32;  }
        static inline auto constrain_spsr64  (uint64 v) { return  v                 & ~res0_spsr64;  }
        static inline auto constrain_tcr32   (uint64 v) { return  v                 & ~res0_tcr32;   }
        static inline auto constrain_tcr64   (uint64 v) { return  v                 & ~res0_tcr64;   }

        static inline void fill_info_regs (uint64 &hcr, uint64 &vpidr, uint64 &vmpidr, uintptr_t (&x)[31])
        {
            hcr    = constrain_hcr (0);
            vpidr  = midr;
            vmpidr = mpidr;

            x[0]  = feat_cpu64[0]; x[1] = feat_cpu64[1];
            x[2]  = feat_dbg64[0]; x[3] = feat_dbg64[1];
            x[4]  = feat_isa64[0]; x[5] = feat_isa64[1]; x[6] = feat_isa64[2];
            x[7]  = feat_mem64[0]; x[8] = feat_mem64[1]; x[9] = feat_mem64[2];
            x[10] = feat_sve64[0];

            x[16] = static_cast<uint64>(feat_cpu32[1]) << 32 | feat_cpu32[0];
            x[17] = static_cast<uint64>(feat_dbg32[0]) << 32 | feat_cpu32[2];
            x[18] = static_cast<uint64>(feat_isa32[0]) << 32 | feat_dbg32[1];
            x[19] = static_cast<uint64>(feat_isa32[2]) << 32 | feat_isa32[1];
            x[20] = static_cast<uint64>(feat_isa32[4]) << 32 | feat_isa32[3];
            x[21] = static_cast<uint64>(feat_isa32[6]) << 32 | feat_isa32[5];
            x[22] = static_cast<uint64>(feat_mem32[1]) << 32 | feat_mem32[0];
            x[23] = static_cast<uint64>(feat_mem32[3]) << 32 | feat_mem32[2];
            x[24] = static_cast<uint64>(feat_mem32[5]) << 32 | feat_mem32[4];

            x[29] = static_cast<uint64>(feat_mfp32[1]) << 32 | feat_mfp32[0];
            x[30] =                                            feat_mfp32[2];
        }

        static void init (unsigned, unsigned);
};
