/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * Great Red Owns Many ACres of Sand 
 */

#ifndef _lrutil_h
#define _lrutil_h

static char *SRCID_lrutil_h = "$Id$";

#include <math.h>
#include "typedefs.h"
#include "complex.h"

extern void set_LRconsts(FILE *log,real r1,real rc,rvec box,t_forcerec *fr);
/* Set constants necessary for Long Range electrostatics calculations */

extern real gk(real k,real rc,real r1);
/* Compute the Ghat function for a single k-value */

extern void pr_scalar_gk(char *fn,int nx,int ny,int nz,rvec box,real ***ghat);

extern real calc_dx2(rvec xi,rvec xj,rvec box);

extern void calc_dx(rvec xi,rvec xj,rvec box,rvec dx);

extern real phi_sr(FILE *log,int nj,rvec x[],real charge[],real rc,real r1,
		   rvec box,real phi[],t_block *excl,rvec f_sr[]);

extern real shiftfunction(real r1,real rc,real R);

extern real spreadfunction(real r1,real rc,real R);

extern real potential(real r1,real rc,real R);

extern void calc_ener(FILE *fp,char *title,bool bHeader,
		      int nmol,int natoms,
		      real phi[],real charge[],t_block *excl);

extern real calc_LRcorrections(FILE *fp,int start,int natoms,real r1,real rc,
			       real charge[],t_block *excl,rvec x[],rvec f[]);
/* Calculate the self energy and forces
 * when using long range electrostatics methods.
 * Part of this is a constant, it is computed only once and stored in
 * a local variable. The remainder is computed every step.
 * No PBC is taken into account!
 */

extern void calc_weights(int nx,int ny,int nz,
			 rvec x,rvec box,rvec invh,ivec ixyz,real WXYZ[]);

static void calc_lll(rvec box,rvec lll)
{
  lll[XX] = 2.0*M_PI/box[XX];
  lll[YY] = 2.0*M_PI/box[YY];
  lll[ZZ] = 2.0*M_PI/box[ZZ];
}

static void calc_k(rvec lll,int ix,int iy,int iz,int nx,int ny,int nz,rvec k)
{
#define IDX(i,n,x)  (i<=n/2) ? (i*x) : ((i-n)*x)
  k[XX] = IDX(ix,nx,lll[XX]);
  k[YY] = IDX(iy,ny,lll[YY]);
  k[ZZ] = IDX(iz,nz,lll[ZZ]);
#undef IDX
}

/******************************************************************
 *
 *   PLOTTING ROUTINES FOR DEBUGGING
 *
 ******************************************************************/
 
extern void plot_phi(char *fn,rvec box,int natoms,rvec x[],real phi[]);
/* Plot potential (or whatever) in a postscript matrix */

extern void print_phi(char *fn,int natoms,rvec x[],real phi[]);
/* Print to a text file in x y phi format */

extern void plot_qtab(char *fn,int nx,int ny,int nz,real ***qtab);
/* Plot a charge table to a postscript matrix */

extern void write_grid_pqr(char *fn,int nx,int ny,int nz,real ***phi);
extern void write_pqr(char *fn,t_atoms *atoms,rvec x[],real phi[],real dx);
/* Write a pdb file where the potential phi is printed as B-factor (for
 * viewing with rasmol). All atoms are moved over a distance dx in the X 
 * direction, to enable viewing of two data sets simultaneously with rasmol
 */

/******************************************************************
 *
 *   ROUTINES FOR GHAT MANIPULATION
 *
 ******************************************************************/
 
extern void symmetrize_ghat(int nx,int ny,int nz,real ***ghat);
/* Symmetrize the Ghat function. It is assumed that the 
 * first octant of the Ghat function is either read or generated
 * (all k-vectors from 0..nx/2 0..ny/2 0..nz/2).
 * Since Gk depends on the absolute value of k only, 
 * symmetry operations may shorten the time to generate it.
 */
 
extern void mk_ghat(FILE *fp,int nx,int ny,int nz,real ***ghat,
		    rvec box,real r1,real rc,bool bSym);
/* Generate a Ghat function from scratch. The ghat grid should
 * be allocated using the mk_rgrid function. When bSym, only
 * the first octant of the function is generated by direct calculation
 * and the above mentioned function is called for computing the rest.
 */

extern real ***rd_ghat(FILE *log,char *fn,ivec igrid,rvec gridspacing,
		       rvec beta,int *porder,real *rshort,real *rlong);
/* Read a Ghat function from a file as generated by the program
 * mk_ghat. The grid size (number of grid points) is returned in
 * igrid, the space between grid points in gridspacing,
 * beta is a constant that determines the contribution of first
 * and second neighbours in the grid to the force
 * (See Luty et al. JCP 103 (1995) 3014)
 * porder determines whether 8 (when porder = 1) or 27 (when
 * porder = 2) neighbouring grid points are used for spreading
 * the charge.
 * rshort and rlong are the lengths used for generating the Ghat
 * function.
 */
		  
extern void wr_ghat(char *fn,int n1max,int n2max,int n3max,real h1,
		    real h2,real h3,real ***ghat,int nalias,
		    int porder,int niter,bool bSym,rvec beta,
		    real r1,real rc,real pval,real zval,real eref,real qopt);
/* Write a ghat file. (see above) */

extern void pr_scalar_gk(char *fn,int nx,int ny,int nz,rvec box,real ***ghat);

extern real analyse_diff(FILE *log,int natom,rvec ffour[],rvec fpppm[],
			 real phi_f[],real phi_p[],real phi_sr[],
			 char *fcorr,char *pcorr,
			 char *ftotcorr,char *ptotcorr);
/* Analyse difference between forces from fourier (_f) and other (_p)
 * LR solvers (and potential also).
 * If the filenames are given, xvgr files are written.
 * returns the root mean square error in the force.
 */

#endif
