/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014,2015, by the GROMACS development team, led by
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
#include <stdlib.h>
#include <immintrin.h>
#include <array>
#include "nbnxn_internal.h"
#include "nbnxn_atomdata.h"
#include "nb_verlet.h"
#include "nbnxn_kernels/simd_2xnn/nbnxn_kernel_simd_2xnn.h"
#include "gromacs/legacyheaders/gmx_omp_nthreads.h"
#include "gromacs/pbcutil/pbc.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/math/vec.h"
#include "nb_verlet_simd_offload.h"
#include "packdata.h"
#include "external/pfun/pfun.h"
#include <unistd.h>

gmx_offload static gmx_bool bRefreshNbl_array[2]    = {TRUE};
static float                offload_signal_array[2] = {0};

#define REUSE alloc_if(0) free_if(0)
#define ALLOC alloc_if(1) free_if(0)
#define FREE  alloc_if(0) free_if(1)

#define NUM_OFFLOAD_BUFFERS 21

class offload_buffer
{
public:
    int    nbl_buffer_size;
    int    ci_buffer_size ;
    int    sci_buffer_size;
    int    cj_buffer_size ;
    int    cj4_buffer_size;
    size_t current_packet_in_size;
    size_t current_packet_out_size;
    nbnxn_pairlist_set_t   *nbl_lists;
    nbnxn_pairlist_t       *nbl_buffer;
    nbnxn_ci_t             *ci_buffer;
    nbnxn_sci_t            *sci_buffer;
    nbnxn_cj_t             *cj_buffer;
    nbnxn_cj4_t            *cj4_buffer;
    int                    *type_buffer;
    real                   *lj_comb_buffer;
    real                   *q_buffer;
    size_t   phi_buffer_sizes[9];
    char                   *cpu_in_packet;
    char                   *cpu_out_packet;
    char                   *phi_in_packet;
    char                   *phi_out_packet;
    gmx_offload
    offload_buffer() :nbl_buffer_size(0),
    		          ci_buffer_size(0),
	    		      sci_buffer_size(0),
		              cj_buffer_size(0),
			          cj4_buffer_size(0),
                      current_packet_in_size(0),
                      current_packet_out_size(0),
					  nbl_lists(NULL),
                      nbl_buffer(NULL),
                      ci_buffer(NULL),
                      sci_buffer(NULL),
                      cj_buffer(NULL),
                      cj4_buffer(NULL),
                      type_buffer(NULL),
                      lj_comb_buffer(NULL),
                      q_buffer(NULL),
					  phi_buffer_sizes{0,0,0,0,0,0,0,0,0},
                      cpu_in_packet(NULL),
                      cpu_out_packet(NULL),
                      phi_in_packet(NULL),
                      phi_out_packet(NULL) {}
};

gmx_offload static offload_buffer offload_buffers_array[2];

typedef struct offload_unpack_data_struct
{
    char *out_packet_addr;
    void *cpu_buffers[4];
} offload_unpack_data;

static offload_unpack_data unpack_data_array[2];

// "Mirror" malloc with corresponding renew and free. Memory is allocated on both
// host and coprocessor, and the two are linked to support offloading operations.

char *mmalloc(size_t s, void *off_ptr)
{
    char *p;
    snew_aligned(p, s, 64);
    char *off_ptr_val;
#pragma offload target(mic:0) nocopy(off_ptr_val:length(s) ALLOC preallocated targetptr)
    {
        snew_aligned(off_ptr_val, s, 64);
    }
    *(void **)off_ptr = off_ptr_val;
    return p;
}

void mfree(char *p, void *off_ptr_val)
{
#pragma offload target(mic:0) nocopy(off_ptr_val:length(0) FREE preallocated targetptr)
    {
        sfree_aligned(off_ptr_val);
    }
    sfree_aligned(p);
}

// Helper method for copying packet buffer into an external buffer.
// Allocates buffer and updates both the passed buffer pointer and
// passed buffer size as needed. Advances iter to the next buffer.
template<typename T>
gmx_offload
T *refresh_buffer(T **buf_ptr, size_t *bsize, packet_iter *iter)
{
    if (size(iter) <= 0)
    {
        next<T>(iter);
    }
    else if (size(iter) <= *bsize)
    {
        cnext<T>(iter, *buf_ptr);
    }
    else
    {
        if (*buf_ptr != NULL)
        {
            sfree_aligned(*buf_ptr);
        }
        *bsize = 2*size(iter);
        *buf_ptr   = anext<T>(iter, 2);
    }

    return *buf_ptr;
}

osig nbnxn_kernel_simd_2xnn_offload(t_forcerec *fr,
                                    interaction_const_t *ic,
                                    gmx_enerdata_t *enerd,
                                    int flags, int ilocality,
                                    int clearF,
                                    t_nrnb *nrnb)
{
    nonbonded_verlet_group_t                *nbvg      = &fr->nbv->grp[ilocality];

    offload_buffer &obuf = offload_buffers_array[nbvg->nbat->id];
    int    &nbl_buffer_size = obuf.nbl_buffer_size;
    int    &ci_buffer_size  = obuf.ci_buffer_size;
    int    &sci_buffer_size = obuf.sci_buffer_size;
    int    &cj_buffer_size  = obuf.cj_buffer_size;
    int    &cj4_buffer_size = obuf.cj4_buffer_size;
    size_t &current_packet_in_size = obuf.current_packet_in_size;
    size_t &current_packet_out_size = obuf.current_packet_out_size;
    nbnxn_pairlist_set_t *&nbl_lists        = obuf.nbl_lists;
    nbnxn_pairlist_t     *&nbl_buffer       = obuf.nbl_buffer;
    nbnxn_ci_t           *&ci_buffer        = obuf.ci_buffer;
    nbnxn_sci_t          *&sci_buffer       = obuf.sci_buffer;
    nbnxn_cj_t           *&cj_buffer        = obuf.cj_buffer;
    nbnxn_cj4_t          *&cj4_buffer       = obuf.cj4_buffer;
    int                  *&type_buffer      = obuf.type_buffer;
    real                 *&lj_comb_buffer   = obuf.lj_comb_buffer;
    real                 *&q_buffer         = obuf.q_buffer;
    size_t (&phi_buffer_sizes)[9]  = obuf.phi_buffer_sizes;
    char                 *&cpu_in_packet    = obuf.cpu_in_packet;
    char                 *&cpu_out_packet   = obuf.cpu_out_packet;
    char                 *&phi_in_packet    = obuf.phi_in_packet;
    char                 *&phi_out_packet   = obuf.phi_out_packet;

    nbl_lists = &nbvg->nbl_lists;
    gmx_bool bRefreshNbl = bRefreshNbl_array[nbvg->nbat->id];
    if (bRefreshNbl)
    {
        int                nbl_buffer_size_req = nbvg->nbl_lists.nnbl;
        int                ci_buffer_size_req  = 0;
        int                sci_buffer_size_req = 0;
        int                cj_buffer_size_req  = 0;
        int                cj4_buffer_size_req = 0;
        nbnxn_pairlist_t **nbl                 = nbl_lists->nbl;
        int                i;
        for (i = 0; i < nbl_buffer_size_req; i++)
        {
            ci_buffer_size_req   += nbl[i]->nci;
            sci_buffer_size_req  += nbl[i]->nsci;
            cj_buffer_size_req   += nbl[i]->ncj;
            cj4_buffer_size_req  += nbl[i]->ncj4;
        }
        if (nbl_buffer_size_req > nbl_buffer_size)
        {
            if (nbl_buffer_size > 0)
            {
                sfree_aligned(nbl_buffer);
            }
            snew_aligned(nbl_buffer, sizeof(nbnxn_pairlist_t)*nbl_buffer_size_req, 64);
            nbl_buffer_size = nbl_buffer_size_req;
        }
        if (ci_buffer_size_req > ci_buffer_size)
        {
            if (ci_buffer_size > 0)
            {
                sfree_aligned(ci_buffer);
            }
            snew_aligned(ci_buffer, sizeof(nbnxn_ci_t)*ci_buffer_size_req, 64);
            ci_buffer_size = ci_buffer_size_req;
        }
        if (sci_buffer_size_req > sci_buffer_size)
        {
            if (sci_buffer_size > 0)
            {
                sfree_aligned(sci_buffer);
            }
            snew_aligned(sci_buffer, sizeof(nbnxn_sci_t)*sci_buffer_size_req, 64);
            sci_buffer_size = sci_buffer_size_req;
        }
        if (cj_buffer_size_req > cj_buffer_size)
        {
            if (cj_buffer_size > 0)
            {
                sfree_aligned(cj_buffer);
            }
            snew_aligned(cj_buffer, sizeof(nbnxn_cj_t)*cj_buffer_size_req, 64);
            cj_buffer_size = cj_buffer_size_req;
        }
        if (cj4_buffer_size_req > cj4_buffer_size)
        {
            if (cj4_buffer_size > 0)
            {
                sfree_aligned(cj4_buffer);
            }
            snew_aligned(cj4_buffer, sizeof(nbnxn_cj4_t)*cj4_buffer_size_req, 64);
            cj4_buffer_size = cj4_buffer_size_req;
        }

        int ci_offset  = 0;
        int sci_offset = 0;
        int cj_offset  = 0;
        int cj4_offset = 0;
        for (i = 0; i < nbvg->nbl_lists.nnbl; i++)
        {
            memcpy(nbl_buffer + i, nbl[i], sizeof(nbnxn_pairlist_t));
            memcpy(ci_buffer + ci_offset, nbl[i]->ci, nbl[i]->nci * sizeof(nbnxn_ci_t));
            ci_offset += nbl[i]->nci;
            memcpy(sci_buffer + sci_offset, nbl[i]->sci, nbl[i]->nsci * sizeof(nbnxn_sci_t));
            sci_offset += nbl[i]->nsci;
            memcpy(cj_buffer + cj_offset, nbl[i]->cj, nbl[i]->ncj * sizeof(nbnxn_cj_t));
            cj_offset += nbl[i]->ncj;
            memcpy(cj4_buffer + cj4_offset, nbl[i]->cj4, nbl[i]->ncj4 * sizeof(nbnxn_cj4_t));
            cj4_offset += nbl[i]->ncj4;
        }
    }

    packet_buffer ibuffers[NUM_OFFLOAD_BUFFERS];
    ibuffers[0] =  (packet_buffer){
        nbl_lists, sizeof(nbnxn_pairlist_set_t) * (bRefreshNbl ? 1 : 0)
    };
    ibuffers[1] =  (packet_buffer){
        nbl_buffer, sizeof(nbnxn_pairlist_t) * (bRefreshNbl ? nbl_buffer_size : 0)
    };
    ibuffers[2] =  (packet_buffer){
        ci_buffer, sizeof(nbnxn_ci_t) * (bRefreshNbl ? ci_buffer_size : 0)
    };
    ibuffers[3] =  (packet_buffer){
        sci_buffer, sizeof(nbnxn_sci_t) * (bRefreshNbl ? sci_buffer_size : 0)
    };
    ibuffers[4] =  (packet_buffer){
        cj_buffer, sizeof(nbnxn_cj_t) * (bRefreshNbl ? cj_buffer_size : 0)
    };
    ibuffers[5] =  (packet_buffer){
        cj4_buffer, sizeof(nbnxn_cj4_t) * (bRefreshNbl ? cj4_buffer_size : 0)
    };
    nbnxn_atomdata_t *nbat = nbvg->nbat;
    ibuffers[6]  =  (packet_buffer){
        nbat, sizeof(nbnxn_atomdata_t)
    };
    ibuffers[7]  =  (packet_buffer){
        nbat->nbfp, sizeof(real) * (nbat->ntype*nbat->ntype*2)
    };
    ibuffers[8]  =  (packet_buffer){
        nbat->nbfp_comb, sizeof(real) * (nbat->comb_rule != ljcrNONE ? nbat->ntype*2 : 0)
    };
    ibuffers[9]  =  (packet_buffer){
        nbat->nbfp_s4, sizeof(real) * (nbat->ntype*nbat->ntype*4)
    };
    ibuffers[10] = (packet_buffer){
        nbat->type, sizeof(int) * (bRefreshNbl ? (nbat->natoms) : 0)
    };
    ibuffers[11] = (packet_buffer){
        nbat->lj_comb, sizeof(real) * (bRefreshNbl ? (nbat->natoms*2) : 0)
    };
    ibuffers[12] = (packet_buffer){
        nbat->q, sizeof(real) * (bRefreshNbl ? (nbat->natoms) : 0)
    };
    ibuffers[13] = (packet_buffer){
        nbat->energrp, sizeof(int) * ((nbat->nenergrp > 1) ? (nbat->natoms/nbat->na_c) : 0)
    };
    ibuffers[14] = (packet_buffer){
        nbat->shift_vec, sizeof(rvec) * SHIFTS
    };
    ibuffers[15] = (packet_buffer){
        nbat->x, sizeof(real) * (nbat->natoms * nbat->xstride)
    };
    ibuffers[16] = (packet_buffer){
        nbat->buffer_flags.flag, sizeof(gmx_bitmask_t) * (nbat->buffer_flags.flag_nalloc)
    };
    ibuffers[17] = (packet_buffer){
        ic, sizeof(interaction_const_t)
    };
    ibuffers[18] = (packet_buffer){
        fr->shift_vec, sizeof(rvec) * SHIFTS
    };
    void *Vc   = enerd->grpp.ener[egCOULSR];
    void *Vvdw = fr->bBHAM ? enerd->grpp.ener[egBHAMSR] : enerd->grpp.ener[egLJSR];
    ibuffers[19] = (packet_buffer){
        Vc, sizeof(real) * (enerd->grpp.nener)
    };
    ibuffers[20] = (packet_buffer){
        Vvdw, sizeof(real) * (enerd->grpp.nener)
    };

    int ewald_excl = nbvg->ewald_excl;

    // Data needed for force and shift reductions
    // TODO: Compiler complains if this buffer is not offloaded, but it shouldn't be necessary.
    size_t packet_in_size = compute_required_size(ibuffers, NUM_OFFLOAD_BUFFERS);
    if (packet_in_size > current_packet_in_size)
    {
        if (cpu_out_packet != NULL)
        {
            mfree(cpu_out_packet, phi_in_packet);
        }
        cpu_out_packet         = mmalloc(2*packet_in_size, &phi_in_packet);
        current_packet_in_size = 2*packet_in_size;
    }
    packdata(cpu_out_packet, ibuffers, NUM_OFFLOAD_BUFFERS);

    packet_buffer obuffers[4];
    obuffers[0] = (packet_buffer){
        nbat->out[0].fshift, sizeof(real) * SHIFTS * DIM
    };
    obuffers[1] = ibuffers[19];    // Vc
    obuffers[2] = ibuffers[20];    // Vvdw
    obuffers[3] = (packet_buffer){ // Force
        nbat->out[0].f, sizeof(real) * nbat->natoms * nbat->fstride
    };
    size_t packet_out_size = compute_required_size(obuffers, 4);
    if (packet_out_size > current_packet_out_size)
    {
        if (cpu_in_packet != NULL)
        {
            mfree(cpu_in_packet, phi_out_packet);
        }
        cpu_in_packet           = mmalloc(2*packet_out_size, &phi_out_packet);
        current_packet_out_size = 2*packet_out_size;
    }

    //TODO: if tables are used, the coul_F and coul_V need to be copied
    //following not needed after. Instead we should call init_simple_exclusion_masks
    //nbat->simd_4xn_diagonal_j_minus_i  = simd_4xn_diagonal_j_minus_i_p;
    //                nbat->simd_2xnn_diagonal_j_minus_i = simd_2xnn_diagonal_j_minus_i_p;
    //                nbat->simd_exclusion_filter1       = simd_exclusion_filter1_p;
    //                nbat->simd_exclusion_filter2       = simd_exclusion_filter2_p;

    // TODO: What about nbl->excl ?

    static PFun offload_pfun_array[2];
    int nbat_id = nbvg->nbat->id;
    PFun &offload_pfun = offload_pfun_array[nbat_id];
    auto offload_fun = [ewald_excl, flags, clearF, packet_in_size, packet_out_size, nbat_id,
						cpu_in_packet, cpu_out_packet, phi_in_packet, phi_out_packet]() {
#pragma offload target(mic:0) \
	nocopy(offload_buffers_array) \
    in (cpu_out_packet[0:packet_in_size] :  into(phi_in_packet[0:packet_in_size]) REUSE targetptr) \
    out(phi_out_packet[0:packet_out_size] : into(cpu_in_packet[0:packet_out_size]) REUSE targetptr)
    // signal(&offload_signal_array[nbat_id])
    {
        offload_buffer &obuf = offload_buffers_array[nbat_id];
        int &nbl_buffer_size = obuf.nbl_buffer_size;
        int &ci_buffer_size  = obuf.ci_buffer_size;
        int &sci_buffer_size = obuf.sci_buffer_size;
        int &cj_buffer_size  = obuf.cj_buffer_size;
        int &cj4_buffer_size = obuf.cj4_buffer_size;
        nbnxn_pairlist_set_t *&nbl_lists      = obuf.nbl_lists;
        nbnxn_pairlist_t     *&nbl_buffer     = obuf.nbl_buffer;
        nbnxn_ci_t           *&ci_buffer      = obuf.ci_buffer;
        nbnxn_sci_t          *&sci_buffer     = obuf.sci_buffer;
        nbnxn_cj_t           *&cj_buffer      = obuf.cj_buffer;
        nbnxn_cj4_t          *&cj4_buffer     = obuf.cj4_buffer;
        int                  *&type_buffer    = obuf.type_buffer;
        real                 *&lj_comb_buffer = obuf.lj_comb_buffer;
        real                 *&q_buffer       = obuf.q_buffer;
        size_t (&phi_buffer_sizes)[9]  = obuf.phi_buffer_sizes;

        // Unpack data
        packet_iter *it;
        smalloc(it, sizeof(packet_iter));

        create_packet_iter(phi_in_packet, it);
        // Memory for nbl_lists->nbl is handled by the Phi. So we store
        // the value in case refresh overwrites it and restore it later.
        nbnxn_pairlist_t **nbl_ptr = NULL;
        if (nbl_lists != NULL)
        {
            nbl_ptr = nbl_lists->nbl;
        }
        dprintf(2, "BR: %p\n", nbl_lists);
        refresh_buffer<nbnxn_pairlist_set_t>(&nbl_lists, &phi_buffer_sizes[0], it);
        dprintf(2, "AR: %p\n", nbl_lists);
        refresh_buffer<nbnxn_pairlist_t>(&nbl_buffer, &phi_buffer_sizes[1], it);
        refresh_buffer<nbnxn_ci_t>(&ci_buffer, &phi_buffer_sizes[2], it);
        refresh_buffer<nbnxn_sci_t>(&sci_buffer, &phi_buffer_sizes[3], it);
        refresh_buffer<nbnxn_cj_t>(&cj_buffer, &phi_buffer_sizes[4], it);
        refresh_buffer<nbnxn_cj4_t>(&cj4_buffer, &phi_buffer_sizes[5], it);
        nbnxn_atomdata_t *nbat  = next<nbnxn_atomdata_t>(it);
        nbat->nbfp              = next<real>(it);
        nbat->nbfp_comb         = next<real>(it);
        nbat->nbfp_s4           = next<real>(it);
        nbat->type              = refresh_buffer<int>(&type_buffer, &phi_buffer_sizes[6], it);
        nbat->lj_comb           = refresh_buffer<real>(&lj_comb_buffer, &phi_buffer_sizes[7], it);
        nbat->q                 = refresh_buffer<real>(&q_buffer, &phi_buffer_sizes[8], it);
        nbat->energrp           = next<int>(it);
        nbat->shift_vec         = next<rvec>(it);
        nbat->x                 = next<real>(it);
        nbnxn_atomdata_t *phi_nbat = get_nbat_for_offload(nbat->id);
        nbat->out               = phi_nbat->out;
        nbat->simd_2xnn_diagonal_j_minus_i = phi_nbat->simd_2xnn_diagonal_j_minus_i;
        nbat->simd_exclusion_filter1 = phi_nbat->simd_exclusion_filter1;
        nbat->simd_exclusion_filter2 = phi_nbat->simd_exclusion_filter2;
        nbat->buffer_flags.flag = next<gmx_bitmask_t>(it);
        interaction_const_t *ic_buffer = next<interaction_const_t>(it);
        rvec                *shift_vec = next<rvec>(it);
        // TODO: Remove from package - not used.
        real                *Vc   = next<real>(it);
        real                *Vvdw = next<real>(it);
        sfree(it);

        // Neighbor list pointer assignments
        int ci_offset  = 0;
        int sci_offset = 0;
        int cj_offset  = 0;
        int cj4_offset = 0;

        // Restore nbl_lists->nbl or allocate if first time
        nbl_lists->nbl = nbl_ptr;
        if (nbl_lists->nbl == NULL)
        {
            nbl_lists->nbl = (nbnxn_pairlist_t **)malloc(sizeof(nbnxn_pairlist_t *)*nbl_lists->nnbl);
        }

        int i;
        for (i = 0; i < nbl_lists->nnbl; i++)
        {
            nbl_lists->nbl[i] = nbl_buffer + i;
            nbnxn_pairlist_t *nbl = nbl_lists->nbl[i];
            nbl->ci     = ci_buffer  + ci_offset;
            nbl->sci    = sci_buffer + sci_offset;
            nbl->cj     = cj_buffer  + cj_offset;
            nbl->cj4    = cj4_buffer + cj4_offset;
            ci_offset  += nbl->nci;
            sci_offset += nbl->nsci;
            cj_offset  += nbl->ncj;
            cj4_offset += nbl->ncj4;
        }

        // End unpacking of data and start actual computing
        /*TODO: ic: table (only if tables are used)

                        verify that those marked as in/out are really only input/output
                        do outputs need to be zeroed?

                        the numa issue for nbl_lists might also be important for MIC so we might want to do a manual allocation
         */
        nbnxn_kernel_simd_2xnn(nbl_lists,
                               // static information (e.g. charges)
                                           //ic seems to be all static. is fr->ic
                               nbat, ic_buffer,
                               ewald_excl, //might depend on Neighbor list or is static
                               shift_vec,  //depends on box size (changes usually with neighbor list)
                               flags,
                               clearF,
                               NULL,  // fshift not used when nnbl > 1
                               Vc,    //output
                               Vvdw); //output
        bRefreshNbl_array[nbat_id] = FALSE;

        // Force and shift reductions
        nbnxn_atomdata_add_nbat_f_to_f_treereduce(nbat, gmx_omp_nthreads_get(emntNonbonded));

        packet_buffer phi_buffers[4];
        phi_buffers[0] = (packet_buffer){
            nbat->out[0].fshift, sizeof(real) * SHIFTS * DIM
        };
        phi_buffers[1] = get_buffer(phi_in_packet, 19);
        phi_buffers[2] = get_buffer(phi_in_packet, 20);
        phi_buffers[3] = (packet_buffer){
            nbat->out[0].f, sizeof(real) * nbat->natoms * nbat->fstride
        };
        packdata(phi_out_packet, phi_buffers, 4);
    };};
    PFunTask<decltype((offload_fun))> *offload_task = new PFunTask<decltype((offload_fun))>(offload_fun);
    offload_pfun.run(offload_task);
    offload_pfun.wait();
    offload_unpack_data &unpack_data = unpack_data_array[nbat->id];
    unpack_data.out_packet_addr = cpu_in_packet;
    unpack_data.cpu_buffers[0]  = nbat->out[0].fshift;
    unpack_data.cpu_buffers[1]  = Vc;
    unpack_data.cpu_buffers[2]  = Vvdw;
    unpack_data.cpu_buffers[3]  = nbat->out[0].f;
    return nbat->id;
}

void wait_for_offload(osig os)
{
// #pragma offload_wait target(mic:0) wait(&offload_signal_array[os])
	offload_unpack_data unpack_data = unpack_data_array[os];
    unpackdata(unpack_data.out_packet_addr, unpack_data.cpu_buffers, 4);
}

void setRefreshNblForOffload()
{
    bRefreshNbl_array[0] = TRUE;
    bRefreshNbl_array[1] = TRUE;
}

gmx_bool offloadedKernelEnabled(int kernel_type)
{
#if defined GMX_OFFLOAD && defined GMX_NBNXN_SIMD_2XNN
    return (kernel_type == nbnxnk4xN_SIMD_2xNN);
#else
    return FALSE;
#endif
}
