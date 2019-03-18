/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2016,2017,2018,2019, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */

#include "gmxpre.h"

#include "gromacs/mdlib/gmx_omp_nthreads.h"
#include "gromacs/nbnxm/nbnxm.h"
#include "gromacs/nbnxm/pairlist.h"
#include "gromacs/utility/gmxassert.h"

#include "pairlistsets.h"
#include "kernels_reference/kernel_ref_prune.h"
#include "kernels_simd_2xmm/kernel_prune.h"
#include "kernels_simd_4xm/kernel_prune.h"

void
PairlistSets::dispatchPruneKernel(const Nbnxm::InteractionLocality  iLocality,
                                  const nbnxn_atomdata_t           *nbat,
                                  const rvec                       *shift_vec,
                                  const Nbnxm::KernelType           kernelType)
{
    pairlistSet(iLocality).dispatchPruneKernel(nbat, shift_vec, kernelType);
}

void
PairlistSet::dispatchPruneKernel(const nbnxn_atomdata_t  *nbat,
                                 const rvec              *shift_vec,
                                 const Nbnxm::KernelType  kernelType)
{
    const real rlistInner = params_.rlistInner;

    GMX_ASSERT(cpuLists_[0].ciOuter.size() >= cpuLists_[0].ci.size(),
               "Here we should either have an empty ci list or ciOuter should be >= ci");

    int gmx_unused nthreads = gmx_omp_nthreads_get(emntNonbonded);
    GMX_ASSERT(nthreads == static_cast<gmx::index>(cpuLists_.size()),
               "The number of threads should match the number of lists");
#pragma omp parallel for schedule(static) num_threads(nthreads)
    for (int i = 0; i < nthreads; i++)
    {
        NbnxnPairlistCpu *nbl = &cpuLists_[i];

        switch (kernelType)
        {
            case Nbnxm::KernelType::Cpu4xN_Simd_4xN:
                nbnxn_kernel_prune_4xn(nbl, nbat, shift_vec, rlistInner);
                break;
            case Nbnxm::KernelType::Cpu4xN_Simd_2xNN:
                nbnxn_kernel_prune_2xnn(nbl, nbat, shift_vec, rlistInner);
                break;
            case Nbnxm::KernelType::Cpu4x4_PlainC:
                nbnxn_kernel_prune_ref(nbl, nbat, shift_vec, rlistInner);
                break;
            default:
                GMX_RELEASE_ASSERT(false, "kernel type not handled (yet)");
        }
    }
}

void
nonbonded_verlet_t::dispatchPruneKernelCpu(const Nbnxm::InteractionLocality  iLocality,
                                           const rvec                       *shift_vec)
{
    pairlistSets_->dispatchPruneKernel(iLocality, nbat.get(), shift_vec, kernelSetup_.kernelType);
}

void nonbonded_verlet_t::dispatchPruneKernelGpu(int64_t step)
{
    const bool stepIsEven = (pairlistSets().numStepsWithPairlist(step) % 2 == 0);

    Nbnxm::gpu_launch_kernel_pruneonly(gpu_nbv,
                                       stepIsEven ? Nbnxm::InteractionLocality::Local : Nbnxm::InteractionLocality::NonLocal,
                                       pairlistSets().params().numRollingPruningParts);
}
