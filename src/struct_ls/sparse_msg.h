/*BHEADER**********************************************************************
 * Copyright (c) 2008,  Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * This file is part of HYPRE.  See file COPYRIGHT for details.
 *
 * HYPRE is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * $Revision: 2.6 $
 ***********************************************************************EHEADER*/




/******************************************************************************
 *
 * Header info for the SparseMSG solver
 *
 *****************************************************************************/

#ifndef hypre_SparseMSG_HEADER
#define hypre_SparseMSG_HEADER

/*--------------------------------------------------------------------------
 * hypre_SparseMSGData:
 *--------------------------------------------------------------------------*/

typedef struct
{
   MPI_Comm              comm;
                      
   double                tol;
   int                   max_iter;
   int                   rel_change;
   int                   zero_guess;
   int                   jump;

   int                   relax_type;     /* type of relaxation to use */
   double                jacobi_weight;  /* weighted jacobi weight */
   int                   usr_jacobi_weight; /* indicator flag for user weight */

   int                   num_pre_relax;  /* number of pre relaxation sweeps */
   int                   num_post_relax; /* number of post relaxation sweeps */
   int                   num_fine_relax; /* number of fine relaxation sweeps */

   int                   num_grids[3];   /* number of grids in each dim */
   int    		 num_all_grids;
   int    		 num_levels;
                      
   hypre_StructGrid    **grid_array;
   hypre_StructGrid    **Px_grid_array;
   hypre_StructGrid    **Py_grid_array;
   hypre_StructGrid    **Pz_grid_array;

   double               *data;
   hypre_StructMatrix  **A_array;
   hypre_StructMatrix  **Px_array;
   hypre_StructMatrix  **Py_array;
   hypre_StructMatrix  **Pz_array;
   hypre_StructMatrix  **RTx_array;
   hypre_StructMatrix  **RTy_array;
   hypre_StructMatrix  **RTz_array;
   hypre_StructVector  **b_array;
   hypre_StructVector  **x_array;

   /* temp vectors */
   hypre_StructVector  **t_array;
   hypre_StructVector  **r_array;
   hypre_StructVector  **e_array;

   hypre_StructVector  **visitx_array;
   hypre_StructVector  **visity_array;
   hypre_StructVector  **visitz_array;
   int                  *grid_on;

   void                **relax_array;
   void                **matvec_array;
   void                **restrictx_array;
   void                **restricty_array;
   void                **restrictz_array;
   void                **interpx_array;
   void                **interpy_array;
   void                **interpz_array;

   /* log info (always logged) */
   int                   num_iterations;
   int                   time_index;
   int                   print_level;

   /* additional log info (logged when `logging' > 0) */
   int                   logging;
   double               *norms;
   double               *rel_norms;

} hypre_SparseMSGData;

/*--------------------------------------------------------------------------
 * Utility routines:
 *--------------------------------------------------------------------------*/

#define hypre_SparseMSGMapIndex(lx, ly, lz, nl, index) \
index = (lx) + ((ly) * nl[0]) + ((lz) * nl[0] * nl[1])

#endif