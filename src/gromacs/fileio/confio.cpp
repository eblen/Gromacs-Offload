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
#include "gmxpre.h"

#include "confio.h"

#include <cstdio>
#include <cstring>

#include "gromacs/fileio/espio.h"
#include "gromacs/fileio/filenm.h"
#include "gromacs/fileio/g96io.h"
#include "gromacs/fileio/gmxfio.h"
#include "gromacs/fileio/groio.h"
#include "gromacs/fileio/pdbio.h"
#include "gromacs/fileio/tpxio.h"
#include "gromacs/fileio/trx.h"
#include "gromacs/fileio/trxio.h"
#include "gromacs/math/vec.h"
#include "gromacs/topology/atomprop.h"
#include "gromacs/topology/atoms.h"
#include "gromacs/topology/block.h"
#include "gromacs/topology/mtop_util.h"
#include "gromacs/topology/symtab.h"
#include "gromacs/topology/topology.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/smalloc.h"

void write_sto_conf_indexed(const char *outfile, const char *title,
                            t_atoms *atoms,
                            rvec x[], rvec *v, int ePBC, matrix box,
                            atom_id nindex, atom_id index[])
{
    FILE       *out;
    int         ftp;
    t_trxframe  fr;

    ftp = fn2ftp(outfile);
    switch (ftp)
    {
        case efGRO:
            out = gmx_fio_fopen(outfile, "w");
            write_hconf_indexed_p(out, title, atoms, nindex, index, 3, x, v, box);
            gmx_fio_fclose(out);
            break;
        case efG96:
            clear_trxframe(&fr, TRUE);
            fr.bTitle = TRUE;
            fr.title  = title;
            fr.natoms = atoms->nr;
            fr.bAtoms = TRUE;
            fr.atoms  = atoms;
            fr.bX     = TRUE;
            fr.x      = x;
            if (v)
            {
                fr.bV = TRUE;
                fr.v  = v;
            }
            fr.bBox = TRUE;
            copy_mat(box, fr.box);
            out = gmx_fio_fopen(outfile, "w");
            write_g96_conf(out, &fr, nindex, index);
            gmx_fio_fclose(out);
            break;
        case efPDB:
        case efBRK:
        case efENT:
        case efPQR:
            out = gmx_fio_fopen(outfile, "w");
            write_pdbfile_indexed(out, title, atoms, x, ePBC, box, ' ', -1, nindex, index, NULL, TRUE);
            gmx_fio_fclose(out);
            break;
        case efESP:
            out = gmx_fio_fopen(outfile, "w");
            write_espresso_conf_indexed(out, title, atoms, nindex, index, x, v, box);
            gmx_fio_fclose(out);
            break;
        case efTPR:
            gmx_fatal(FARGS, "Sorry, can not write a topology to %s", outfile);
            break;
        default:
            gmx_incons("Not supported in write_sto_conf_indexed");
    }
}

void write_sto_conf(const char *outfile, const char *title, t_atoms *atoms,
                    rvec x[], rvec *v, int ePBC, matrix box)
{
    FILE       *out;
    int         ftp;
    t_trxframe  fr;

    ftp = fn2ftp(outfile);
    switch (ftp)
    {
        case efGRO:
            write_conf_p(outfile, title, atoms, 3, x, v, box);
            break;
        case efG96:
            clear_trxframe(&fr, TRUE);
            fr.bTitle = TRUE;
            fr.title  = title;
            fr.natoms = atoms->nr;
            fr.bAtoms = TRUE;
            fr.atoms  = atoms;
            fr.bX     = TRUE;
            fr.x      = x;
            if (v)
            {
                fr.bV = TRUE;
                fr.v  = v;
            }
            fr.bBox = TRUE;
            copy_mat(box, fr.box);
            out = gmx_fio_fopen(outfile, "w");
            write_g96_conf(out, &fr, -1, NULL);
            gmx_fio_fclose(out);
            break;
        case efPDB:
        case efBRK:
        case efENT:
            out = gmx_fio_fopen(outfile, "w");
            write_pdbfile(out, title, atoms, x, ePBC, box, ' ', -1, NULL, TRUE);
            gmx_fio_fclose(out);
            break;
        case efESP:
            out = gmx_fio_fopen(outfile, "w");
            write_espresso_conf_indexed(out, title, atoms, atoms->nr, NULL, x, v, box);
            gmx_fio_fclose(out);
            break;
        case efTPR:
            gmx_fatal(FARGS, "Sorry, can not write a topology to %s", outfile);
            break;
        default:
            gmx_incons("Not supported in write_sto_conf");
    }
}

void write_sto_conf_mtop(const char *outfile, const char *title,
                         gmx_mtop_t *mtop,
                         rvec x[], rvec *v, int ePBC, matrix box)
{
    int     ftp;
    FILE   *out;
    t_atoms atoms;

    ftp = fn2ftp(outfile);
    switch (ftp)
    {
        case efGRO:
            out = gmx_fio_fopen(outfile, "w");
            write_hconf_mtop(out, title, mtop, 3, x, v, box);
            gmx_fio_fclose(out);
            break;
        default:
            /* This is a brute force approach which requires a lot of memory.
             * We should implement mtop versions of all writing routines.
             */
            atoms = gmx_mtop_global_atoms(mtop);

            write_sto_conf(outfile, title, &atoms, x, v, ePBC, box);

            done_atom(&atoms);
            break;
    }
}

void get_stx_coordnum(const char *infile, int *natoms)
{
    FILE      *in;
    int        ftp, tpxver, tpxgen;
    t_trxframe fr;
    char       g96_line[STRLEN+1];

    ftp = fn2ftp(infile);
    range_check(ftp, 0, efNR);
    switch (ftp)
    {
        case efGRO:
            get_coordnum(infile, natoms);
            break;
        case efG96:
            in        = gmx_fio_fopen(infile, "r");
            fr.title  = NULL;
            fr.natoms = -1;
            fr.atoms  = NULL;
            fr.x      = NULL;
            fr.v      = NULL;
            fr.f      = NULL;
            *natoms   = read_g96_conf(in, infile, &fr, g96_line);
            gmx_fio_fclose(in);
            break;
        case efPDB:
        case efBRK:
        case efENT:
            in = gmx_fio_fopen(infile, "r");
            get_pdb_coordnum(in, natoms);
            gmx_fio_fclose(in);
            break;
        case efESP:
            *natoms = get_espresso_coordnum(infile);
            break;
        case efTPR:
        {
            t_tpxheader tpx;

            read_tpxheader(infile, &tpx, TRUE, &tpxver, &tpxgen);
            *natoms = tpx.natoms;
            break;
        }
        default:
            gmx_fatal(FARGS, "File type %s not supported in get_stx_coordnum",
                      ftp2ext(ftp));
    }
}

static void tpx_make_chain_identifiers(t_atoms *atoms, t_block *mols)
{
    int  m, a, a0, a1, r;
    char c, chainid;
    int  chainnum;

    /* We always assign a new chain number, but save the chain id characters
     * for larger molecules.
     */
#define CHAIN_MIN_ATOMS 15

    chainnum = 0;
    chainid  = 'A';
    for (m = 0; m < mols->nr; m++)
    {
        a0 = mols->index[m];
        a1 = mols->index[m+1];
        if ((a1-a0 >= CHAIN_MIN_ATOMS) && (chainid <= 'Z'))
        {
            c = chainid;
            chainid++;
        }
        else
        {
            c = ' ';
        }
        for (a = a0; a < a1; a++)
        {
            atoms->resinfo[atoms->atom[a].resind].chainnum = chainnum;
            atoms->resinfo[atoms->atom[a].resind].chainid  = c;
        }
        chainnum++;
    }

    /* Blank out the chain id if there was only one chain */
    if (chainid == 'B')
    {
        for (r = 0; r < atoms->nres; r++)
        {
            atoms->resinfo[r].chainid = ' ';
        }
    }
}

void read_stx_conf(const char *infile, char *title, t_atoms *atoms,
                   rvec x[], rvec *v, int *ePBC, matrix box)
{
    FILE       *in;
    gmx_mtop_t *mtop;
    t_topology  top;
    t_trxframe  fr;
    int         i, ftp, natoms;
    char        g96_line[STRLEN+1];

    if (atoms->nr == 0)
    {
        fprintf(stderr, "Warning: Number of atoms in %s is 0\n", infile);
    }
    else if (atoms->atom == NULL)
    {
        gmx_mem("Uninitialized array atom");
    }

    if (ePBC)
    {
        *ePBC = -1;
    }

    ftp = fn2ftp(infile);
    switch (ftp)
    {
        case efGRO:
            read_whole_conf(infile, title, atoms, x, v, box);
            break;
        case efG96:
            fr.title  = NULL;
            fr.natoms = atoms->nr;
            fr.atoms  = atoms;
            fr.x      = x;
            fr.v      = v;
            fr.f      = NULL;
            in        = gmx_fio_fopen(infile, "r");
            read_g96_conf(in, infile, &fr, g96_line);
            gmx_fio_fclose(in);
            copy_mat(fr.box, box);
            std::strncpy(title, fr.title, STRLEN);
            break;
        case efPDB:
        case efBRK:
        case efENT:
            read_pdb_conf(infile, title, atoms, x, ePBC, box, TRUE, NULL);
            break;
        case efESP:
            read_espresso_conf(infile, title, atoms, x, v, box);
            break;
        case efTPR:
            snew(mtop, 1);
            i = read_tpx(infile, NULL, box, &natoms, x, v, NULL, mtop);
            if (ePBC)
            {
                *ePBC = i;
            }

            strcpy(title, *(mtop->name));

            /* Free possibly allocated memory */
            done_atom(atoms);

            *atoms = gmx_mtop_global_atoms(mtop);
            top    = gmx_mtop_t_to_t_topology(mtop);
            tpx_make_chain_identifiers(atoms, &top.mols);

            sfree(mtop);
            /* The strings in the symtab are still in use in the returned t_atoms
             * structure, so we should not free them. But there is no place to put the
             * symbols; the only choice is to leak the memory...
             * So we clear the symbol table before freeing the topology structure. */
            free_symtab(&top.symtab);
            done_top(&top);

            break;
        default:
            gmx_incons("Not supported in read_stx_conf");
    }
}

static void done_gmx_groups_t(gmx_groups_t *g)
{
    int i;

    for (i = 0; (i < egcNR); i++)
    {
        if (NULL != g->grps[i].nm_ind)
        {
            sfree(g->grps[i].nm_ind);
            g->grps[i].nm_ind = NULL;
        }
        if (NULL != g->grpnr[i])
        {
            sfree(g->grpnr[i]);
            g->grpnr[i] = NULL;
        }
    }
    /* The contents of this array is in symtab, don't free it here */
    sfree(g->grpname);
}

gmx_bool read_tps_conf(const char *infile, char *title, t_topology *top, int *ePBC,
                       rvec **x, rvec **v, matrix box, gmx_bool bMass)
{
    t_tpxheader      header;
    int              natoms, i, version, generation;
    gmx_bool         bTop, bXNULL = FALSE;
    gmx_mtop_t      *mtop;
    gmx_atomprop_t   aps;

    bTop  = fn2bTPX(infile);
    *ePBC = -1;
    if (bTop)
    {
        read_tpxheader(infile, &header, TRUE, &version, &generation);
        if (x)
        {
            snew(*x, header.natoms);
        }
        if (v)
        {
            snew(*v, header.natoms);
        }
        snew(mtop, 1);
        *ePBC = read_tpx(infile, NULL, box, &natoms,
                         (x == NULL) ? NULL : *x, (v == NULL) ? NULL : *v, NULL, mtop);
        *top = gmx_mtop_t_to_t_topology(mtop);
        /* In this case we need to throw away the group data too */
        done_gmx_groups_t(&mtop->groups);
        sfree(mtop);
        std::strcpy(title, *top->name);
        tpx_make_chain_identifiers(&top->atoms, &top->mols);
    }
    else
    {
        get_stx_coordnum(infile, &natoms);
        init_t_atoms(&top->atoms, natoms, (fn2ftp(infile) == efPDB));
        if (x == NULL)
        {
            snew(x, 1);
            bXNULL = TRUE;
        }
        snew(*x, natoms);
        if (v)
        {
            snew(*v, natoms);
        }
        read_stx_conf(infile, title, &top->atoms, *x, (v == NULL) ? NULL : *v, ePBC, box);
        if (bXNULL)
        {
            sfree(*x);
            sfree(x);
        }
        if (bMass)
        {
            aps = gmx_atomprop_init();
            for (i = 0; (i < natoms); i++)
            {
                if (!gmx_atomprop_query(aps, epropMass,
                                        *top->atoms.resinfo[top->atoms.atom[i].resind].name,
                                        *top->atoms.atomname[i],
                                        &(top->atoms.atom[i].m)))
                {
                    if (debug)
                    {
                        fprintf(debug, "Can not find mass for atom %s %d %s, setting to 1\n",
                                *top->atoms.resinfo[top->atoms.atom[i].resind].name,
                                top->atoms.resinfo[top->atoms.atom[i].resind].nr,
                                *top->atoms.atomname[i]);
                    }
                }
            }
            gmx_atomprop_destroy(aps);
        }
        top->idef.ntypes = -1;
    }

    return bTop;
}
