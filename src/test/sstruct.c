/*BHEADER**********************************************************************
 * Copyright (c) 2008,  Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * This file is part of HYPRE.  See file COPYRIGHT for details.
 *
 * HYPRE is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * $Revision: 1.38 $
 ***********************************************************************EHEADER*/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "_hypre_utilities.h"
#include "HYPRE_sstruct_ls.h"
#include "HYPRE_struct_ls.h"
#include "HYPRE_krylov.h"
#include "_hypre_sstruct_mv.h"

/* begin lobpcg */

#include <time.h>
 
#include "fortran_matrix.h"
#include "HYPRE_lobpcg.h"
#include "interpreter.h"
#include "multivector.h"
#include "HYPRE_MatvecFunctions.h"
#include "HYPRE_sstruct_int.h"

#define NO_SOLVER -9198

/* end lobpcg */
 
#define DEBUG 0

/*--------------------------------------------------------------------------
 * Data structures
 *--------------------------------------------------------------------------*/

char infile_default[50] = "sstruct.in.default";

typedef int Index[3];

/*------------------------------------------------------------
 * ProblemIndex:
 *
 * The index has extra information stored in entries 3-8 that
 * determine how the index gets "mapped" to finer index spaces.
 *
 * NOTE: For implementation convenience, the index is "pre-shifted"
 * according to the values in entries 6,7,8.  The following discussion
 * describes how "un-shifted" indexes are mapped, because that is a
 * more natural way to think about this mapping problem, and because
 * that is the convention used in the input file for this code.  The
 * reason that pre-shifting is convenient is because it makes the true
 * value of the index on the unrefined index space readily available
 * in entries 0-2, hence, all operations on that unrefined space are
 * straightforward.  Also, the only time that the extra mapping
 * information is needed is when an index is mapped to a new refined
 * index space, allowing us to isolate the mapping details to the
 * routine MapProblemIndex.  The only other effected routine is
 * SScanProblemIndex, which takes the user input and pre-shifts it.
 *
 * - Entries 3,4,5 have values of either 0 or 1 that indicate
 *   whether to map an index "to the left" or "to the right".
 *   Here is a 1D diagram:
 *
 *    --  |     *     |    unrefined index space
 *   |
 *    --> | * | . | * |    refined index space (factor = 3)
 *          0       1
 *
 *   The '*' index on the unrefined index space gets mapped to one of
 *   the '*' indexes on the refined space based on the value (0 or 1)
 *   of the relevent entry (3,4, or 5).  The actual mapping formula is
 *   as follows (with refinement factor, r):
 *
 *   mapped_index[i] = r*index[i] + (r-1)*index[i+3]
 *
 * - Entries 6,7,8 contain "shift" information.  The shift is
 *   simply added to the mapped index just described.  So, the
 *   complete mapping formula is as follows:
 *
 *   mapped_index[i] = r*index[i] + (r-1)*index[i+3] + index[i+6]
 *
 *------------------------------------------------------------*/

typedef int ProblemIndex[9];

typedef struct
{
   /* for GridSetExtents */
   int                    nboxes;
   ProblemIndex          *ilowers;
   ProblemIndex          *iuppers;
   int                   *boxsizes;
   int                    max_boxsize;

   /* for GridSetVariables */
   int                    nvars;
   HYPRE_SStructVariable *vartypes;

   /* for GridAddVariables */
   int                    add_nvars;
   ProblemIndex          *add_indexes;
   HYPRE_SStructVariable *add_vartypes;

   /* for GridSetNeighborPart (and defunct GridSetNeighborBox) */
   int                    glue_nboxes;
   int                   *glue_defunct;
   ProblemIndex          *glue_ilowers;
   ProblemIndex          *glue_iuppers;
   int                   *glue_nbor_parts;
   ProblemIndex          *glue_nbor_ilowers;
   ProblemIndex          *glue_nbor_iuppers;
   Index                 *glue_index_maps;
   Index                 *glue_index_dirs;
   int                   *glue_primaries;

   /* for GraphSetStencil */
   int                   *stencil_num;

   /* for GraphAddEntries */
   int                    graph_nboxes;
   ProblemIndex          *graph_ilowers;
   ProblemIndex          *graph_iuppers;
   Index                 *graph_strides;
   int                   *graph_vars;
   int                   *graph_to_parts;
   ProblemIndex          *graph_to_ilowers;
   ProblemIndex          *graph_to_iuppers;
   Index                 *graph_to_strides;
   int                   *graph_to_vars;
   Index                 *graph_index_maps;
   Index                 *graph_index_signs;
   int                   *graph_entries;
   double                *graph_values;
   int                   *graph_boxsizes;

   /* MatrixSetValues */
   int                    matset_nboxes;
   ProblemIndex          *matset_ilowers;
   ProblemIndex          *matset_iuppers;
   Index                 *matset_strides;
   int                   *matset_vars;
   int                   *matset_entries;
   double                *matset_values;

   /* MatrixAddToValues */
   int                    matadd_nboxes;
   ProblemIndex          *matadd_ilowers;
   ProblemIndex          *matadd_iuppers;
   int                   *matadd_vars;
   int                   *matadd_nentries;
   int                  **matadd_entries;
   double               **matadd_values;

   /* FEMatrixSetValues */
   int                    fe_matset_nboxes;
   ProblemIndex          *fe_matset_ilowers;
   ProblemIndex          *fe_matset_iuppers;
   int                   *fe_matset_nrows;
   int                  **fe_matset_rows;
   int                   *fe_matset_ncols;
   int                  **fe_matset_cols;
   double               **fe_matset_values;

   /* FEMatrixAddToValues */
   int                    fe_matadd_nboxes;
   ProblemIndex          *fe_matadd_ilowers;
   ProblemIndex          *fe_matadd_iuppers;
   int                   *fe_matadd_nrows;
   int                  **fe_matadd_rows;
   int                   *fe_matadd_ncols;
   int                  **fe_matadd_cols;
   double               **fe_matadd_values;

   Index                  periodic;

} ProblemPartData;
 
typedef struct
{
   int              ndim;
   int              nparts;
   ProblemPartData *pdata;
   int              max_boxsize;

   int              nstencils;
   int             *stencil_sizes;
   Index          **stencil_offsets;
   int            **stencil_vars;
   double         **stencil_values;

   int              fe_stencil_size;
   Index           *fe_stencil_offsets;
   int             *fe_stencil_vars;
   double         **fe_stencil_values;
   int            **fe_stencil_entries;  /* corresponding stencil entries */

   int              symmetric_num;
   int             *symmetric_parts;
   int             *symmetric_vars;
   int             *symmetric_to_vars;
   int             *symmetric_booleans;

   int              ns_symmetric;

   int              npools;
   int             *pools;   /* array of size nparts */

} ProblemData;
 
/*--------------------------------------------------------------------------
 * Compute new box based on variable type
 *--------------------------------------------------------------------------*/

int
GetVariableBox( Index  cell_ilower,
                Index  cell_iupper,
                int    int_vartype,
                Index  var_ilower,
                Index  var_iupper )
{
   int ierr = 0;
   HYPRE_SStructVariable  vartype = (HYPRE_SStructVariable) int_vartype;

   var_ilower[0] = cell_ilower[0];
   var_ilower[1] = cell_ilower[1];
   var_ilower[2] = cell_ilower[2];
   var_iupper[0] = cell_iupper[0];
   var_iupper[1] = cell_iupper[1];
   var_iupper[2] = cell_iupper[2];

   switch(vartype)
   {
      case HYPRE_SSTRUCT_VARIABLE_CELL:
      var_ilower[0] -= 0; var_ilower[1] -= 0; var_ilower[2] -= 0;
      break;
      case HYPRE_SSTRUCT_VARIABLE_NODE:
      var_ilower[0] -= 1; var_ilower[1] -= 1; var_ilower[2] -= 1;
      break;
      case HYPRE_SSTRUCT_VARIABLE_XFACE:
      var_ilower[0] -= 1; var_ilower[1] -= 0; var_ilower[2] -= 0;
      break;
      case HYPRE_SSTRUCT_VARIABLE_YFACE:
      var_ilower[0] -= 0; var_ilower[1] -= 1; var_ilower[2] -= 0;
      break;
      case HYPRE_SSTRUCT_VARIABLE_ZFACE:
      var_ilower[0] -= 0; var_ilower[1] -= 0; var_ilower[2] -= 1;
      break;
      case HYPRE_SSTRUCT_VARIABLE_XEDGE:
      var_ilower[0] -= 0; var_ilower[1] -= 1; var_ilower[2] -= 1;
      break;
      case HYPRE_SSTRUCT_VARIABLE_YEDGE:
      var_ilower[0] -= 1; var_ilower[1] -= 0; var_ilower[2] -= 1;
      break;
      case HYPRE_SSTRUCT_VARIABLE_ZEDGE:
      var_ilower[0] -= 1; var_ilower[1] -= 1; var_ilower[2] -= 0;
      break;
      case HYPRE_SSTRUCT_VARIABLE_UNDEFINED:
      break;
   }

   return ierr;
}

/*--------------------------------------------------------------------------
 * Read routines
 *--------------------------------------------------------------------------*/

int
SScanIntArray( char  *sdata_ptr,
               char **sdata_ptr_ptr,
               int    size,
               int   *array )
{
   int i;

   sdata_ptr += strspn(sdata_ptr, " \t\n[");
   for (i = 0; i < size; i++)
   {
      array[i] = strtol(sdata_ptr, &sdata_ptr, 10);
   }
   sdata_ptr += strcspn(sdata_ptr, "]") + 1;

   *sdata_ptr_ptr = sdata_ptr;
   return 0;
}

int
SScanDblArray( char   *sdata_ptr,
               char  **sdata_ptr_ptr,
               int     size,
               double *array )
{
   int i;
                                                                                                                           
   sdata_ptr += strspn(sdata_ptr, " \t\n[");
   for (i = 0; i < size; i++)
   {
      array[i] = strtod(sdata_ptr, &sdata_ptr);
   }
   sdata_ptr += strcspn(sdata_ptr, "]") + 1;
                                                                                                                           
   *sdata_ptr_ptr = sdata_ptr;
   return 0;
}
                                                                                                                           
int
SScanProblemIndex( char          *sdata_ptr,
                   char         **sdata_ptr_ptr,
                   int            ndim,
                   ProblemIndex   index )
{
   int  i;
   char sign[3];

   /* initialize index array */
   for (i = 0; i < 9; i++)
   {
      index[i]   = 0;
   }

   sdata_ptr += strspn(sdata_ptr, " \t\n(");
   switch (ndim)
   {
      case 1:
      sscanf(sdata_ptr, "%d%c",
             &index[0], &sign[0]);
      break;

      case 2:
      sscanf(sdata_ptr, "%d%c%d%c",
             &index[0], &sign[0], &index[1], &sign[1]);
      break;

      case 3:
      sscanf(sdata_ptr, "%d%c%d%c%d%c",
             &index[0], &sign[0], &index[1], &sign[1], &index[2], &sign[2]);
      break;
   }
   sdata_ptr += strcspn(sdata_ptr, ":)");
   if ( *sdata_ptr == ':' )
   {
      /* read in optional shift */
      sdata_ptr += 1;
      switch (ndim)
      {
         case 1:
            sscanf(sdata_ptr, "%d", &index[6]);
            break;
            
         case 2:
            sscanf(sdata_ptr, "%d%d", &index[6], &index[7]);
            break;
            
         case 3:
            sscanf(sdata_ptr, "%d%d%d", &index[6], &index[7], &index[8]);
            break;
      }
      /* pre-shift the index */
      for (i = 0; i < ndim; i++)
      {
         index[i] += index[i+6];
      }
   }
   sdata_ptr += strcspn(sdata_ptr, ")") + 1;

   for (i = 0; i < ndim; i++)
   {
      if (sign[i] == '+')
      {
         index[i+3] = 1;
      }
   }

   *sdata_ptr_ptr = sdata_ptr;
   return 0;
}

int
ReadData( char         *filename,
          ProblemData  *data_ptr )
{
   ProblemData        data;
   ProblemPartData    pdata;

   int                myid;
   FILE              *file;

   char              *sdata = NULL;
   char              *sdata_line;
   char              *sdata_ptr;
   int                sdata_size;
   int                size;
   int                memchunk = 10000;
   int                maxline  = 250;

   char               key[250];

   int                part, var, s, entry, i, j, k, il, iu;

   /*-----------------------------------------------------------
    * Read data file from process 0, then broadcast
    *-----------------------------------------------------------*/
 
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   if (myid == 0)
   {
      if ((file = fopen(filename, "r")) == NULL)
      {
         printf("Error: can't open input file %s\n", filename);
         exit(1);
      }

      /* allocate initial space, and read first input line */
      sdata_size = 0;
      sdata = hypre_TAlloc(char, memchunk);
      sdata_line = fgets(sdata, maxline, file);

      s= 0;
      while (sdata_line != NULL)
      {
         sdata_size += strlen(sdata_line) + 1;

         /* allocate more space, if necessary */
         if ((sdata_size + maxline) > s)
         {
            sdata = hypre_TReAlloc(sdata, char, (sdata_size + memchunk));
            s= sdata_size + memchunk;
         }
         
         /* read the next input line */
         sdata_line = fgets((sdata + sdata_size), maxline, file);
      }
   }

   /* broadcast the data size */
   MPI_Bcast(&sdata_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

   /* broadcast the data */
   sdata = hypre_TReAlloc(sdata, char, sdata_size);
   MPI_Bcast(sdata, sdata_size, MPI_CHAR, 0, MPI_COMM_WORLD);

   /*-----------------------------------------------------------
    * Parse the data and fill ProblemData structure
    *-----------------------------------------------------------*/

   data.max_boxsize = 0;
   data.nstencils = 0;
   data.fe_stencil_size = 0;
   data.symmetric_num = 0;
   data.symmetric_parts    = NULL;
   data.symmetric_vars     = NULL;
   data.symmetric_to_vars  = NULL;
   data.symmetric_booleans = NULL;
   data.ns_symmetric = 0;

   sdata_line = sdata;
   while (sdata_line < (sdata + sdata_size))
   {
      sdata_ptr = sdata_line;
      
      if ( ( sscanf(sdata_ptr, "%s", key) > 0 ) && ( sdata_ptr[0] != '#' ) )
      {
         sdata_ptr += strcspn(sdata_ptr, " \t\n");

         if ( strcmp(key, "GridCreate:") == 0 )
         {
            data.ndim = strtol(sdata_ptr, &sdata_ptr, 10);
            data.nparts = strtol(sdata_ptr, &sdata_ptr, 10);
            data.pdata = hypre_CTAlloc(ProblemPartData, data.nparts);
         }
         else if ( strcmp(key, "GridSetExtents:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.nboxes % 10) == 0)
            {
               size = pdata.nboxes + 10;
               pdata.ilowers =
                  hypre_TReAlloc(pdata.ilowers, ProblemIndex, size);
               pdata.iuppers =
                  hypre_TReAlloc(pdata.iuppers, ProblemIndex, size);
               pdata.boxsizes =
                  hypre_TReAlloc(pdata.boxsizes, int, size);
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.ilowers[pdata.nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.iuppers[pdata.nboxes]);
            /* check use of +- in GridSetExtents */
            il = 1;
            iu = 1;
            for (i = 0; i < data.ndim; i++)
            {
               il *= pdata.ilowers[pdata.nboxes][i+3];
               iu *= pdata.iuppers[pdata.nboxes][i+3];
            }
            if ( (il != 0) || (iu != 1) )
            {
               printf("Error: Invalid use of `+-' in GridSetExtents\n");
               exit(1);
            }
            pdata.boxsizes[pdata.nboxes] = 1;
            for (i = 0; i < 3; i++)
            {
               pdata.boxsizes[pdata.nboxes] *=
                  (pdata.iuppers[pdata.nboxes][i] -
                   pdata.ilowers[pdata.nboxes][i] + 2);
            }
            pdata.max_boxsize =
               hypre_max(pdata.max_boxsize, pdata.boxsizes[pdata.nboxes]);
            pdata.nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "GridSetVariables:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            pdata.nvars = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.vartypes = hypre_CTAlloc(HYPRE_SStructVariable, pdata.nvars);
            SScanIntArray(sdata_ptr, &sdata_ptr,
                          pdata.nvars, (int *) pdata.vartypes);
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "GridAddVariables:") == 0 )
         {
            /* TODO */
            printf("GridAddVariables not yet implemented!\n");
            exit(1);
         }
         else if ( strcmp(key, "GridSetNeighborPart:") == 0 ||
                   strcmp(key, "GridSetNeighborBox:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.glue_nboxes % 10) == 0)
            {
               size = pdata.glue_nboxes + 10;
               pdata.glue_defunct =
                  hypre_TReAlloc(pdata.glue_nbor_parts, int, size);
               pdata.glue_ilowers =
                  hypre_TReAlloc(pdata.glue_ilowers, ProblemIndex, size);
               pdata.glue_iuppers =
                  hypre_TReAlloc(pdata.glue_iuppers, ProblemIndex, size);
               pdata.glue_nbor_parts =
                  hypre_TReAlloc(pdata.glue_nbor_parts, int, size);
               pdata.glue_nbor_ilowers =
                  hypre_TReAlloc(pdata.glue_nbor_ilowers, ProblemIndex, size);
               pdata.glue_nbor_iuppers =
                  hypre_TReAlloc(pdata.glue_nbor_iuppers, ProblemIndex, size);
               pdata.glue_index_maps =
                  hypre_TReAlloc(pdata.glue_index_maps, Index, size);
               pdata.glue_index_dirs =
                  hypre_TReAlloc(pdata.glue_index_dirs, Index, size);
               pdata.glue_primaries =
                  hypre_TReAlloc(pdata.glue_primaries, int, size);
            }
            pdata.glue_defunct[pdata.glue_nboxes] = 0;
            if ( strcmp(key, "GridSetNeighborBox:") == 0 )
            {
               pdata.glue_defunct[pdata.glue_nboxes] = 1;
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.glue_ilowers[pdata.glue_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.glue_iuppers[pdata.glue_nboxes]);
            pdata.glue_nbor_parts[pdata.glue_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.glue_nbor_ilowers[pdata.glue_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.glue_nbor_iuppers[pdata.glue_nboxes]);
            SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim,
                          pdata.glue_index_maps[pdata.glue_nboxes]);
            for (i = data.ndim; i < 3; i++)
            {
               pdata.glue_index_maps[pdata.glue_nboxes][i] = i;
            }
            if (!pdata.glue_defunct[pdata.glue_nboxes])
            {
               SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim,
                             pdata.glue_index_dirs[pdata.glue_nboxes]);
               for (i = data.ndim; i < 3; i++)
               {
                  pdata.glue_index_dirs[pdata.glue_nboxes][i] = 1;
               }
            }
            sdata_ptr += strcspn(sdata_ptr, ":\t\n");
            if ( *sdata_ptr == ':' )
            {
               /* read in optional primary indicator */
               sdata_ptr += 1;
               pdata.glue_primaries[pdata.glue_nboxes] =
                  strtol(sdata_ptr, &sdata_ptr, 10);
            }
            else
            {
               pdata.glue_primaries[pdata.glue_nboxes] = -1;
               sdata_ptr -= 1;
            }
            pdata.glue_nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "GridSetPeriodic:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim, pdata.periodic);
            for (i = data.ndim; i < 3; i++)
            {
               pdata.periodic[i] = 0;
            }
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "StencilCreate:") == 0 )
         {
            if (data.fe_stencil_size > 0)
            {
               printf("Stencil and FEStencil cannot be used together\n");
               exit(1);
            }
            data.nstencils = strtol(sdata_ptr, &sdata_ptr, 10);
            data.stencil_sizes   = hypre_CTAlloc(int, data.nstencils);
            data.stencil_offsets = hypre_CTAlloc(Index *, data.nstencils);
            data.stencil_vars    = hypre_CTAlloc(int *, data.nstencils);
            data.stencil_values  = hypre_CTAlloc(double *, data.nstencils);
            SScanIntArray(sdata_ptr, &sdata_ptr,
                          data.nstencils, data.stencil_sizes);
            for (s = 0; s < data.nstencils; s++)
            {
               data.stencil_offsets[s] =
                  hypre_CTAlloc(Index, data.stencil_sizes[s]);
               data.stencil_vars[s] =
                  hypre_CTAlloc(int, data.stencil_sizes[s]);
               data.stencil_values[s] =
                  hypre_CTAlloc(double, data.stencil_sizes[s]);
            }
         }
         else if ( strcmp(key, "StencilSetEntry:") == 0 )
         {
            s = strtol(sdata_ptr, &sdata_ptr, 10);
            entry = strtol(sdata_ptr, &sdata_ptr, 10);
            SScanIntArray(sdata_ptr, &sdata_ptr,
                          data.ndim, data.stencil_offsets[s][entry]);
            for (i = data.ndim; i < 3; i++)
            {
               data.stencil_offsets[s][entry][i] = 0;
            }
            data.stencil_vars[s][entry] = strtol(sdata_ptr, &sdata_ptr, 10);
            data.stencil_values[s][entry] = strtod(sdata_ptr, &sdata_ptr);
         }
         else if ( strcmp(key, "FEStencilCreate:") == 0 )
         {
            if (data.nstencils > 0)
            {
               printf("Stencil and FEStencil cannot be used together\n");
               exit(1);
            }
            data.fe_stencil_size = strtol(sdata_ptr, &sdata_ptr, 10);
            data.fe_stencil_offsets =
               hypre_CTAlloc(Index, data.fe_stencil_size);
            data.fe_stencil_vars =
               hypre_CTAlloc(int, data.fe_stencil_size);
            data.fe_stencil_values =
               hypre_CTAlloc(double *, data.fe_stencil_size);
            data.fe_stencil_entries =
               hypre_CTAlloc(int *, data.fe_stencil_size);
            for (i = 0; i < data.fe_stencil_size; i++)
            {
               data.fe_stencil_values[i] =
                  hypre_CTAlloc(double, data.fe_stencil_size);
               data.fe_stencil_entries[i] =
                  hypre_CTAlloc(int, data.fe_stencil_size);
            }
         }
         else if ( strcmp(key, "FEStencilSetRow:") == 0 )
         {
            i = strtol(sdata_ptr, &sdata_ptr, 10);
            SScanIntArray(sdata_ptr, &sdata_ptr,
                          data.ndim, data.fe_stencil_offsets[i]);
            for (k = data.ndim; k < 3; k++)
            {
               data.fe_stencil_offsets[i][k] = 0;
            }
            data.fe_stencil_vars[i] = strtol(sdata_ptr, &sdata_ptr, 10);
            SScanDblArray(sdata_ptr, &sdata_ptr,
                          data.fe_stencil_size, data.fe_stencil_values[i]);
         }
         else if ( strcmp(key, "GraphSetStencil:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            var = strtol(sdata_ptr, &sdata_ptr, 10);
            s = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if (pdata.stencil_num == NULL)
            {
               pdata.stencil_num = hypre_CTAlloc(int, pdata.nvars);
            }
            pdata.stencil_num[var] = s;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "GraphAddEntries:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.graph_nboxes % 10) == 0)
            {
               size = pdata.graph_nboxes + 10;
               pdata.graph_ilowers =
                  hypre_TReAlloc(pdata.graph_ilowers, ProblemIndex, size);
               pdata.graph_iuppers =
                  hypre_TReAlloc(pdata.graph_iuppers, ProblemIndex, size);
               pdata.graph_strides =
                  hypre_TReAlloc(pdata.graph_strides, Index, size);
               pdata.graph_vars =
                  hypre_TReAlloc(pdata.graph_vars, int, size);
               pdata.graph_to_parts =
                  hypre_TReAlloc(pdata.graph_to_parts, int, size);
               pdata.graph_to_ilowers =
                  hypre_TReAlloc(pdata.graph_to_ilowers, ProblemIndex, size);
               pdata.graph_to_iuppers =
                  hypre_TReAlloc(pdata.graph_to_iuppers, ProblemIndex, size);
               pdata.graph_to_strides =
                  hypre_TReAlloc(pdata.graph_to_strides, Index, size);
               pdata.graph_to_vars =
                  hypre_TReAlloc(pdata.graph_to_vars, int, size);
               pdata.graph_index_maps =
                  hypre_TReAlloc(pdata.graph_index_maps, Index, size);
               pdata.graph_index_signs =
                  hypre_TReAlloc(pdata.graph_index_signs, Index, size);
               pdata.graph_entries =
                  hypre_TReAlloc(pdata.graph_entries, int, size);
               pdata.graph_values =
                  hypre_TReAlloc(pdata.graph_values, double, size);
               pdata.graph_boxsizes =
                  hypre_TReAlloc(pdata.graph_boxsizes, int, size);
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.graph_ilowers[pdata.graph_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.graph_iuppers[pdata.graph_nboxes]);
            SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim,
                          pdata.graph_strides[pdata.graph_nboxes]);
            for (i = data.ndim; i < 3; i++)
            {
               pdata.graph_strides[pdata.graph_nboxes][i] = 1;
            }
            pdata.graph_vars[pdata.graph_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.graph_to_parts[pdata.graph_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.graph_to_ilowers[pdata.graph_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.graph_to_iuppers[pdata.graph_nboxes]);
            SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim,
                          pdata.graph_to_strides[pdata.graph_nboxes]);
            for (i = data.ndim; i < 3; i++)
            {
               pdata.graph_to_strides[pdata.graph_nboxes][i] = 1;
            }
            pdata.graph_to_vars[pdata.graph_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim,
                          pdata.graph_index_maps[pdata.graph_nboxes]);
            for (i = data.ndim; i < 3; i++)
            {
               pdata.graph_index_maps[pdata.graph_nboxes][i] = i;
            }
            for (i = 0; i < 3; i++)
            {
               pdata.graph_index_signs[pdata.graph_nboxes][i] = 1;
               if ( pdata.graph_to_iuppers[pdata.graph_nboxes][i] <
                    pdata.graph_to_ilowers[pdata.graph_nboxes][i] )
               {
                  pdata.graph_index_signs[pdata.graph_nboxes][i] = -1;
               }
            }
            pdata.graph_entries[pdata.graph_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.graph_values[pdata.graph_nboxes] =
               strtod(sdata_ptr, &sdata_ptr);
            pdata.graph_boxsizes[pdata.graph_nboxes] = 1;
            for (i = 0; i < 3; i++)
            {
               pdata.graph_boxsizes[pdata.graph_nboxes] *=
                  (pdata.graph_iuppers[pdata.graph_nboxes][i] -
                   pdata.graph_ilowers[pdata.graph_nboxes][i] + 1);
            }
            pdata.graph_nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "MatrixSetSymmetric:") == 0 )
         {
            if ((data.symmetric_num % 10) == 0)
            {
               size = data.symmetric_num + 10;
               data.symmetric_parts =
                  hypre_TReAlloc(data.symmetric_parts, int, size);
               data.symmetric_vars =
                  hypre_TReAlloc(data.symmetric_vars, int, size);
               data.symmetric_to_vars =
                  hypre_TReAlloc(data.symmetric_to_vars, int, size);
               data.symmetric_booleans =
                  hypre_TReAlloc(data.symmetric_booleans, int, size);
            }
            data.symmetric_parts[data.symmetric_num] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            data.symmetric_vars[data.symmetric_num] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            data.symmetric_to_vars[data.symmetric_num] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            data.symmetric_booleans[data.symmetric_num] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            data.symmetric_num++;
         }
         else if ( strcmp(key, "MatrixSetNSSymmetric:") == 0 )
         {
            data.ns_symmetric = strtol(sdata_ptr, &sdata_ptr, 10);
         }
         else if ( strcmp(key, "MatrixSetValues:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.matset_nboxes % 10) == 0)
            {
               size = pdata.matset_nboxes + 10;
               pdata.matset_ilowers =
                  hypre_TReAlloc(pdata.matset_ilowers, ProblemIndex, size);
               pdata.matset_iuppers =
                  hypre_TReAlloc(pdata.matset_iuppers, ProblemIndex, size);
               pdata.matset_strides =
                  hypre_TReAlloc(pdata.matset_strides, Index, size);
               pdata.matset_vars =
                  hypre_TReAlloc(pdata.matset_vars, int, size);
               pdata.matset_entries =
                  hypre_TReAlloc(pdata.matset_entries, int, size);
               pdata.matset_values =
                  hypre_TReAlloc(pdata.matset_values, double, size);
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.matset_ilowers[pdata.matset_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
                              pdata.matset_iuppers[pdata.matset_nboxes]);
            SScanIntArray(sdata_ptr, &sdata_ptr, data.ndim,
                          pdata.matset_strides[pdata.matset_nboxes]);
            for (i = data.ndim; i < 3; i++)
            {
               pdata.matset_strides[pdata.matset_nboxes][i] = 1;
            }
            pdata.matset_vars[pdata.matset_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.matset_entries[pdata.matset_nboxes] =
               strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.matset_values[pdata.matset_nboxes] =
               strtod(sdata_ptr, &sdata_ptr);
            pdata.matset_nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "MatrixAddToValues:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.matadd_nboxes% 10) == 0)
            {
               size = pdata.matadd_nboxes+10;
               pdata.matadd_ilowers=
                  hypre_TReAlloc(pdata.matadd_ilowers, ProblemIndex, size);
               pdata.matadd_iuppers=
                  hypre_TReAlloc(pdata.matadd_iuppers, ProblemIndex, size);
               pdata.matadd_vars=
                  hypre_TReAlloc(pdata.matadd_vars, int, size);
               pdata.matadd_nentries=
                  hypre_TReAlloc(pdata.matadd_nentries, int, size);
               pdata.matadd_entries=
                  hypre_TReAlloc(pdata.matadd_entries, int *, size);
               pdata.matadd_values=
                  hypre_TReAlloc(pdata.matadd_values, double *, size);
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
               pdata.matadd_ilowers[pdata.matadd_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
               pdata.matadd_iuppers[pdata.matadd_nboxes]);
            pdata.matadd_vars[pdata.matadd_nboxes]=
                strtol(sdata_ptr, &sdata_ptr, 10);
            i= strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.matadd_nentries[pdata.matadd_nboxes]= i;
            pdata.matadd_entries[pdata.matadd_nboxes] =
               hypre_TAlloc(int, i);
            SScanIntArray(sdata_ptr, &sdata_ptr, i,
              (int*) pdata.matadd_entries[pdata.matadd_nboxes]);
            pdata.matadd_values[pdata.matadd_nboxes] =
               hypre_TAlloc(double, i);
            SScanDblArray(sdata_ptr, &sdata_ptr, i,
              (double *) pdata.matadd_values[pdata.matadd_nboxes]);
            pdata.matadd_nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "FEMatrixSetValues:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.fe_matset_nboxes% 10) == 0)
            {
               size = pdata.fe_matset_nboxes+10;
               pdata.fe_matset_ilowers=
                  hypre_TReAlloc(pdata.fe_matset_ilowers, ProblemIndex, size);
               pdata.fe_matset_iuppers=
                  hypre_TReAlloc(pdata.fe_matset_iuppers, ProblemIndex, size);
               pdata.fe_matset_nrows=
                  hypre_TReAlloc(pdata.fe_matset_nrows, int, size);
               pdata.fe_matset_rows=
                  hypre_TReAlloc(pdata.fe_matset_rows, int *, size);
               pdata.fe_matset_ncols=
                  hypre_TReAlloc(pdata.fe_matset_ncols, int, size);
               pdata.fe_matset_cols=
                  hypre_TReAlloc(pdata.fe_matset_cols, int *, size);
               pdata.fe_matset_values=
                  hypre_TReAlloc(pdata.fe_matset_values, double *, size);
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
               pdata.fe_matset_ilowers[pdata.fe_matset_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
               pdata.fe_matset_iuppers[pdata.fe_matset_nboxes]);
            i= strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.fe_matset_nrows[pdata.fe_matset_nboxes]= i;
            pdata.fe_matset_rows[pdata.fe_matset_nboxes] = hypre_TAlloc(int, i);
            SScanIntArray(sdata_ptr, &sdata_ptr, i,
              (int*) pdata.fe_matset_rows[pdata.fe_matset_nboxes]);
            j= strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.fe_matset_ncols[pdata.fe_matset_nboxes]= j;
            pdata.fe_matset_cols[pdata.fe_matset_nboxes] = hypre_TAlloc(int, j);
            SScanIntArray(sdata_ptr, &sdata_ptr, j,
              (int*) pdata.fe_matset_cols[pdata.fe_matset_nboxes]);
            pdata.fe_matset_values[pdata.fe_matset_nboxes] =
               hypre_TAlloc(double, i*j);
            SScanDblArray(sdata_ptr, &sdata_ptr, i*j,
              (double *) pdata.fe_matset_values[pdata.fe_matset_nboxes]);
            pdata.fe_matset_nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "FEMatrixAddToValues:") == 0 )
         {
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            pdata = data.pdata[part];
            if ((pdata.fe_matadd_nboxes% 10) == 0)
            {
               size = pdata.fe_matadd_nboxes+10;
               pdata.fe_matadd_ilowers=
                  hypre_TReAlloc(pdata.fe_matadd_ilowers, ProblemIndex, size);
               pdata.fe_matadd_iuppers=
                  hypre_TReAlloc(pdata.fe_matadd_iuppers, ProblemIndex, size);
               pdata.fe_matadd_nrows=
                  hypre_TReAlloc(pdata.fe_matadd_nrows, int, size);
               pdata.fe_matadd_rows=
                  hypre_TReAlloc(pdata.fe_matadd_rows, int *, size);
               pdata.fe_matadd_ncols=
                  hypre_TReAlloc(pdata.fe_matadd_ncols, int, size);
               pdata.fe_matadd_cols=
                  hypre_TReAlloc(pdata.fe_matadd_cols, int *, size);
               pdata.fe_matadd_values=
                  hypre_TReAlloc(pdata.fe_matadd_values, double *, size);
            }
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
               pdata.fe_matadd_ilowers[pdata.fe_matadd_nboxes]);
            SScanProblemIndex(sdata_ptr, &sdata_ptr, data.ndim,
               pdata.fe_matadd_iuppers[pdata.fe_matadd_nboxes]);
            i= strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.fe_matadd_nrows[pdata.fe_matadd_nboxes]= i;
            pdata.fe_matadd_rows[pdata.fe_matadd_nboxes] = hypre_TAlloc(int, i);
            SScanIntArray(sdata_ptr, &sdata_ptr, i,
              (int*) pdata.fe_matadd_rows[pdata.fe_matadd_nboxes]);
            j= strtol(sdata_ptr, &sdata_ptr, 10);
            pdata.fe_matadd_ncols[pdata.fe_matadd_nboxes]= j;
            pdata.fe_matadd_cols[pdata.fe_matadd_nboxes] = hypre_TAlloc(int, j);
            SScanIntArray(sdata_ptr, &sdata_ptr, j,
              (int*) pdata.fe_matadd_cols[pdata.fe_matadd_nboxes]);
            pdata.fe_matadd_values[pdata.fe_matadd_nboxes] =
               hypre_TAlloc(double, i*j);
            SScanDblArray(sdata_ptr, &sdata_ptr, i*j,
              (double *) pdata.fe_matadd_values[pdata.fe_matadd_nboxes]);
            pdata.fe_matadd_nboxes++;
            data.pdata[part] = pdata;
         }
         else if ( strcmp(key, "ProcessPoolCreate:") == 0 )
         {
            data.npools = strtol(sdata_ptr, &sdata_ptr, 10);
            data.pools = hypre_CTAlloc(int, data.nparts);
         }
         else if ( strcmp(key, "ProcessPoolSetPart:") == 0 )
         {
            i = strtol(sdata_ptr, &sdata_ptr, 10);
            part = strtol(sdata_ptr, &sdata_ptr, 10);
            data.pools[part] = i;
         }
      }

      sdata_line += strlen(sdata_line) + 1;
   }

   data.max_boxsize = 0;
   for (part = 0; part < data.nparts; part++)
   {
      data.max_boxsize =
         hypre_max(data.max_boxsize, data.pdata[part].max_boxsize);
   }

   /* compute stencils from fe_stencil */
   if (data.fe_stencil_size)
   {
      Index   offset;
      int     var;
      double  value;

      data.nstencils       = data.pdata[0].nvars;
      data.stencil_sizes   = hypre_CTAlloc(int, data.nstencils);
      data.stencil_offsets = hypre_CTAlloc(Index *, data.nstencils);
      data.stencil_vars    = hypre_CTAlloc(int *, data.nstencils);
      data.stencil_values  = hypre_CTAlloc(double *, data.nstencils);
      /* allocate based on an upper bound */
      size = data.fe_stencil_size * data.fe_stencil_size;
      for (s = 0; s < data.nstencils; s++)
      {
         data.stencil_offsets[s] = hypre_CTAlloc(Index, size);
         data.stencil_vars[s]    = hypre_CTAlloc(int, size);
         data.stencil_values[s]  = hypre_CTAlloc(double, size);
      }

      for (i = 0; i < data.fe_stencil_size; i++)
      {
         s = data.fe_stencil_vars[i];

         for (j = 0; j < data.fe_stencil_size; j++)
         {
            /* shift off-diagonal offset by diagonal */
            for (k = 0; k < 3; k++)
            {
               offset[k] =
                  data.fe_stencil_offsets[j][k] -
                  data.fe_stencil_offsets[i][k];
            }
            var = data.fe_stencil_vars[j];
            value = data.fe_stencil_values[i][j];

            /* search stencil_offsets */
            for (entry = 0; entry < data.stencil_sizes[s]; entry++)
            {
               /* if offset is already in the stencil, break */
               if ( (offset[0] == data.stencil_offsets[s][entry][0]) &&
                    (offset[1] == data.stencil_offsets[s][entry][1]) &&
                    (offset[2] == data.stencil_offsets[s][entry][2]) &&
                    (var == data.stencil_vars[s][entry]) )
               {
                  break;
               }
            }
            /* if this is a new nonzero stencil offset, add it to the stencil */
            if ((entry == data.stencil_sizes[s]) && (value != 0.0))
            {
               data.stencil_offsets[s][entry][0] = offset[0];
               data.stencil_offsets[s][entry][1] = offset[1];
               data.stencil_offsets[s][entry][2] = offset[2];
               data.stencil_vars[s][entry]       = var;
               /* set stencil values to zero to initialize AddTo calls */
               data.stencil_values[s][entry]     = 0.0;
               data.stencil_sizes[s]++;
            }

            if (value != 0.0)
            {
               data.fe_stencil_entries[i][j] = entry;
            }
            else
            {
               data.fe_stencil_entries[i][j] = -1;
            }
         }
      }

      for (part = 0; part < data.nparts; part++)
      {
         pdata = data.pdata[part];
         pdata.stencil_num = hypre_CTAlloc(int, pdata.nvars);
         for (var = 0; var < pdata.nvars; var++)
         {
            pdata.stencil_num[var] = var;
         }
         data.pdata[part] = pdata;
      }
   }

   hypre_TFree(sdata);

   *data_ptr = data; 
   return 0;
}
 
/*--------------------------------------------------------------------------
 * Distribute routines
 *--------------------------------------------------------------------------*/

int
MapProblemIndex( ProblemIndex index,
                 Index        m )
{
   /* un-shift the index */
   index[0] -= index[6];
   index[1] -= index[7];
   index[2] -= index[8];
   /* map the index */
   index[0] = m[0]*index[0] + (m[0]-1)*index[3];
   index[1] = m[1]*index[1] + (m[1]-1)*index[4];
   index[2] = m[2]*index[2] + (m[2]-1)*index[5];
   /* pre-shift the new mapped index */
   index[0] += index[6];
   index[1] += index[7];
   index[2] += index[8];

   return 0;
}

int
IntersectBoxes( ProblemIndex ilower1,
                ProblemIndex iupper1,
                ProblemIndex ilower2,
                ProblemIndex iupper2,
                ProblemIndex int_ilower,
                ProblemIndex int_iupper )
{
   int d, size;

   size = 1;
   for (d = 0; d < 3; d++)
   {
      int_ilower[d] = hypre_max(ilower1[d], ilower2[d]);
      int_iupper[d] = hypre_min(iupper1[d], iupper2[d]);
      size *= hypre_max(0, (int_iupper[d] - int_ilower[d] + 1));
   }

   return size;
}

int
DistributeData( ProblemData   global_data,
                Index        *refine,
                Index        *distribute,
                Index        *block,
                int           num_procs,
                int           myid,
                ProblemData  *data_ptr )
{
   ProblemData      data = global_data;
   ProblemPartData  pdata;
   int             *pool_procs;
   int              np, pid;
   int              pool, part, box, b, p, q, r, i, d;
   int              dmap, sign, size;
   Index            m, mmap, n;
   ProblemIndex     ilower, iupper, int_ilower, int_iupper;

   /* determine first process number in each pool */
   pool_procs = hypre_CTAlloc(int, (data.npools+1));
   for (part = 0; part < data.nparts; part++)
   {
      pool = data.pools[part] + 1;
      np = distribute[part][0] * distribute[part][1] * distribute[part][2];
      pool_procs[pool] = hypre_max(pool_procs[pool], np);

   }
   pool_procs[0] = 0;
   for (pool = 1; pool < (data.npools + 1); pool++)
   {
      pool_procs[pool] = pool_procs[pool - 1] + pool_procs[pool];
   }

   /* check number of processes */
   if (pool_procs[data.npools] != num_procs)
   {
      printf("Error: Invalid number of processes or process topology \n");
      exit(1);
   }

   /* modify part data */
   for (part = 0; part < data.nparts; part++)
   {
      pdata = data.pdata[part];
      pool  = data.pools[part];
      np  = distribute[part][0] * distribute[part][1] * distribute[part][2];
      pid = myid - pool_procs[pool];

      if ( (pid < 0) || (pid >= np) )
      {
         /* none of this part data lives on this process */
         pdata.nboxes = 0;
         pdata.glue_nboxes = 0;
         pdata.graph_nboxes = 0;
         pdata.matset_nboxes = 0;
         pdata.matadd_nboxes = 0;
         for (box = 0; box < pdata.fe_matset_nboxes; box++)
         {
            hypre_TFree(pdata.fe_matset_rows[box]);
            hypre_TFree(pdata.fe_matset_cols[box]);
            hypre_TFree(pdata.fe_matset_values[box]);
         }
         pdata.fe_matset_nboxes = 0;
         for (box = 0; box < pdata.fe_matadd_nboxes; box++)
         {
            hypre_TFree(pdata.fe_matadd_rows[box]);
            hypre_TFree(pdata.fe_matadd_cols[box]);
            hypre_TFree(pdata.fe_matadd_values[box]);
         }
         pdata.fe_matadd_nboxes = 0;
      }
      else
      {
         /* refine boxes */
         m[0] = refine[part][0];
         m[1] = refine[part][1];
         m[2] = refine[part][2];
         if ( (m[0] * m[1] * m[2]) > 1)
         {
            for (box = 0; box < pdata.nboxes; box++)
            {
               MapProblemIndex(pdata.ilowers[box], m);
               MapProblemIndex(pdata.iuppers[box], m);
            }

            for (box = 0; box < pdata.graph_nboxes; box++)
            {
               MapProblemIndex(pdata.graph_ilowers[box], m);
               MapProblemIndex(pdata.graph_iuppers[box], m);
               mmap[0] = m[pdata.graph_index_maps[box][0]];
               mmap[1] = m[pdata.graph_index_maps[box][1]];
               mmap[2] = m[pdata.graph_index_maps[box][2]];
               MapProblemIndex(pdata.graph_to_ilowers[box], mmap);
               MapProblemIndex(pdata.graph_to_iuppers[box], mmap);
            }
            for (box = 0; box < pdata.matset_nboxes; box++)
            {
               MapProblemIndex(pdata.matset_ilowers[box], m);
               MapProblemIndex(pdata.matset_iuppers[box], m);
            }
            for (box = 0; box < pdata.matadd_nboxes; box++)
            {
               MapProblemIndex(pdata.matadd_ilowers[box], m);
               MapProblemIndex(pdata.matadd_iuppers[box], m);
            }
            for (box = 0; box < pdata.fe_matset_nboxes; box++)
            {
               MapProblemIndex(pdata.fe_matset_ilowers[box], m);
               MapProblemIndex(pdata.fe_matset_iuppers[box], m);
            }
            for (box = 0; box < pdata.fe_matadd_nboxes; box++)
            {
               MapProblemIndex(pdata.fe_matadd_ilowers[box], m);
               MapProblemIndex(pdata.fe_matadd_iuppers[box], m);
            }
         }

         /* refine and distribute boxes */
         m[0] = distribute[part][0];
         m[1] = distribute[part][1];
         m[2] = distribute[part][2];
         if ( (m[0] * m[1] * m[2]) > 1)
         {
            p = pid % m[0];
            q = ((pid - p) / m[0]) % m[1];
            r = (pid - p - q*m[0]) / (m[0]*m[1]);

            for (box = 0; box < pdata.nboxes; box++)
            {
               n[0] = pdata.iuppers[box][0] - pdata.ilowers[box][0] + 1;
               n[1] = pdata.iuppers[box][1] - pdata.ilowers[box][1] + 1;
               n[2] = pdata.iuppers[box][2] - pdata.ilowers[box][2] + 1;

               MapProblemIndex(pdata.ilowers[box], m);
               MapProblemIndex(pdata.iuppers[box], m);
               pdata.iuppers[box][0] = pdata.ilowers[box][0] + n[0] - 1;
               pdata.iuppers[box][1] = pdata.ilowers[box][1] + n[1] - 1;
               pdata.iuppers[box][2] = pdata.ilowers[box][2] + n[2] - 1;

               pdata.ilowers[box][0] = pdata.ilowers[box][0] + p*n[0];
               pdata.ilowers[box][1] = pdata.ilowers[box][1] + q*n[1];
               pdata.ilowers[box][2] = pdata.ilowers[box][2] + r*n[2];
               pdata.iuppers[box][0] = pdata.iuppers[box][0] + p*n[0];
               pdata.iuppers[box][1] = pdata.iuppers[box][1] + q*n[1];
               pdata.iuppers[box][2] = pdata.iuppers[box][2] + r*n[2];
            }

            i = 0;
            for (box = 0; box < pdata.graph_nboxes; box++)
            {
               MapProblemIndex(pdata.graph_ilowers[box], m);
               MapProblemIndex(pdata.graph_iuppers[box], m);
               mmap[0] = m[pdata.graph_index_maps[box][0]];
               mmap[1] = m[pdata.graph_index_maps[box][1]];
               mmap[2] = m[pdata.graph_index_maps[box][2]];
               MapProblemIndex(pdata.graph_to_ilowers[box], mmap);
               MapProblemIndex(pdata.graph_to_iuppers[box], mmap);

               for (b = 0; b < pdata.nboxes; b++)
               {
                  /* first convert the box extents based on vartype */
                  GetVariableBox(pdata.ilowers[b], pdata.iuppers[b],
                                 pdata.vartypes[pdata.graph_vars[box]],
                                 ilower, iupper);
                  size = IntersectBoxes(pdata.graph_ilowers[box],
                                        pdata.graph_iuppers[box],
                                        ilower, iupper,
                                        int_ilower, int_iupper);
                  if (size > 0)
                  {
                     /* if there is an intersection, it is the only one */
                     for (d = 0; d < 3; d++)
                     {
                        dmap = pdata.graph_index_maps[box][d];
                        sign = pdata.graph_index_signs[box][d];
                        pdata.graph_to_ilowers[i][dmap] =
                           pdata.graph_to_ilowers[box][dmap] +
                           sign * pdata.graph_to_strides[box][d] *
                           ((int_ilower[d] - pdata.graph_ilowers[box][d]) /
                            pdata.graph_strides[box][d]);
                        pdata.graph_to_iuppers[i][dmap] =
                           pdata.graph_to_iuppers[box][dmap] +
                           sign * pdata.graph_to_strides[box][d] *
                           ((int_iupper[d] - pdata.graph_iuppers[box][d]) /
                            pdata.graph_strides[box][d]);
                        pdata.graph_ilowers[i][d] = int_ilower[d];
                        pdata.graph_iuppers[i][d] = int_iupper[d];
                        pdata.graph_strides[i][d] =
                           pdata.graph_strides[box][d];
                        pdata.graph_to_strides[i][d] =
                           pdata.graph_to_strides[box][d];
                        pdata.graph_index_maps[i][d]  = dmap;
                        pdata.graph_index_signs[i][d] = sign;
                     }
                     for (d = 3; d < 9; d++)
                     {
                        pdata.graph_ilowers[i][d] =
                           pdata.graph_ilowers[box][d];
                        pdata.graph_iuppers[i][d] =
                           pdata.graph_iuppers[box][d];
                        pdata.graph_to_ilowers[i][d] =
                           pdata.graph_to_ilowers[box][d];
                        pdata.graph_to_iuppers[i][d] =
                           pdata.graph_to_iuppers[box][d];
                     }
                     pdata.graph_vars[i]     = pdata.graph_vars[box];
                     pdata.graph_to_parts[i] = pdata.graph_to_parts[box];
                     pdata.graph_to_vars[i]  = pdata.graph_to_vars[box];
                     pdata.graph_entries[i]  = pdata.graph_entries[box];
                     pdata.graph_values[i]   = pdata.graph_values[box];
                     i++;
                     break;
                  }
               }
            }
            pdata.graph_nboxes = i;

            i = 0;
            for (box = 0; box < pdata.matset_nboxes; box++)
            {
               MapProblemIndex(pdata.matset_ilowers[box], m);
               MapProblemIndex(pdata.matset_iuppers[box], m);

               for (b = 0; b < pdata.nboxes; b++)
               {
                  /* first convert the box extents based on vartype */
                  GetVariableBox(pdata.ilowers[b], pdata.iuppers[b],
                                 pdata.vartypes[pdata.matset_vars[box]],
                                 ilower, iupper);
                  size = IntersectBoxes(pdata.matset_ilowers[box],
                                        pdata.matset_iuppers[box],
                                        ilower, iupper,
                                        int_ilower, int_iupper);
                  if (size > 0)
                  {
                     /* if there is an intersection, it is the only one */
                     for (d = 0; d < 3; d++)
                     {
                        pdata.matset_ilowers[i][d] = int_ilower[d];
                        pdata.matset_iuppers[i][d] = int_iupper[d];
                        pdata.matset_strides[i][d] =
                           pdata.matset_strides[box][d];
                     }
                     for (d = 3; d < 9; d++)
                     {
                        pdata.matset_ilowers[i][d] =
                           pdata.matset_ilowers[box][d];
                        pdata.matset_iuppers[i][d] =
                           pdata.matset_iuppers[box][d];
                     }
                     pdata.matset_vars[i]     = pdata.matset_vars[box];
                     pdata.matset_entries[i]  = pdata.matset_entries[box];
                     pdata.matset_values[i]   = pdata.matset_values[box];
                     i++;
                     break;
                  }
               }
            }
            pdata.matset_nboxes = i;

            i = 0;
            for (box = 0; box < pdata.matadd_nboxes; box++)
            {
               MapProblemIndex(pdata.matadd_ilowers[box], m);
               MapProblemIndex(pdata.matadd_iuppers[box], m);

               for (b = 0; b < pdata.nboxes; b++)
               {
                  /* first convert the box extents based on vartype */
                  GetVariableBox(pdata.ilowers[b], pdata.iuppers[b],
                                 pdata.vartypes[pdata.matadd_vars[box]],
                                 ilower, iupper);
                  size = IntersectBoxes(pdata.matadd_ilowers[box],
                                        pdata.matadd_iuppers[box],
                                        ilower, iupper,
                                        int_ilower, int_iupper);
                  if (size > 0)
                  {
                     /* if there is an intersection, it is the only one */
                     for (d = 0; d < 3; d++)
                     {
                        pdata.matadd_ilowers[i][d] = int_ilower[d];
                        pdata.matadd_iuppers[i][d] = int_iupper[d];
                     }
                     for (d = 3; d < 9; d++)
                     {
                        pdata.matadd_ilowers[i][d] =
                           pdata.matadd_ilowers[box][d];
                        pdata.matadd_iuppers[i][d] =
                           pdata.matadd_iuppers[box][d];
                     }
                     pdata.matadd_vars[i]     = pdata.matadd_vars[box];
                     pdata.matadd_nentries[i] = pdata.matadd_nentries[box];
                     pdata.matadd_entries[i]  = pdata.matadd_entries[box];
                     pdata.matadd_values[i]   = pdata.matadd_values[box];
                     i++;
                     break;
                  }
               }
            }
            pdata.matadd_nboxes = i;

            i = 0;
            for (box = 0; box < pdata.fe_matset_nboxes; box++)
            {
               MapProblemIndex(pdata.fe_matset_ilowers[box], m);
               MapProblemIndex(pdata.fe_matset_iuppers[box], m);

               for (b = 0; b < pdata.nboxes; b++)
               {
                  /* fe is cell-based, so no need to convert box extents */
                  size = IntersectBoxes(pdata.fe_matset_ilowers[box],
                                        pdata.fe_matset_iuppers[box],
                                        pdata.ilowers[b], pdata.iuppers[b],
                                        int_ilower, int_iupper);
                  if (size > 0)
                  {
                     /* if there is an intersection, it is the only one */
                     for (d = 0; d < 3; d++)
                     {
                        pdata.fe_matset_ilowers[i][d] = int_ilower[d];
                        pdata.fe_matset_iuppers[i][d] = int_iupper[d];
                     }
                     for (d = 3; d < 9; d++)
                     {
                        pdata.fe_matset_ilowers[i][d] =
                           pdata.fe_matset_ilowers[box][d];
                        pdata.fe_matset_iuppers[i][d] =
                           pdata.fe_matset_iuppers[box][d];
                     }
                     pdata.fe_matset_nrows[i]  = pdata.fe_matset_nrows[box];
                     pdata.fe_matset_rows[i]   = pdata.fe_matset_rows[box];
                     pdata.fe_matset_ncols[i]  = pdata.fe_matset_ncols[box];
                     pdata.fe_matset_cols[i]   = pdata.fe_matset_cols[box];
                     pdata.fe_matset_values[i] = pdata.fe_matset_values[box];
                     i++;
                     break;
                  }
               }
            }
            for (box = i; box < pdata.fe_matset_nboxes; box++)
            {
               hypre_TFree(pdata.fe_matset_rows[box]);
               hypre_TFree(pdata.fe_matset_cols[box]);
               hypre_TFree(pdata.fe_matset_values[box]);
            }
            pdata.fe_matset_nboxes = i;

            i = 0;
            for (box = 0; box < pdata.fe_matadd_nboxes; box++)
            {
               MapProblemIndex(pdata.fe_matadd_ilowers[box], m);
               MapProblemIndex(pdata.fe_matadd_iuppers[box], m);

               for (b = 0; b < pdata.nboxes; b++)
               {
                  /* fe is cell-based, so no need to convert box extents */
                  size = IntersectBoxes(pdata.fe_matadd_ilowers[box],
                                        pdata.fe_matadd_iuppers[box],
                                        pdata.ilowers[b], pdata.iuppers[b],
                                        int_ilower, int_iupper);
                  if (size > 0)
                  {
                     /* if there is an intersection, it is the only one */
                     for (d = 0; d < 3; d++)
                     {
                        pdata.fe_matadd_ilowers[i][d] = int_ilower[d];
                        pdata.fe_matadd_iuppers[i][d] = int_iupper[d];
                     }
                     for (d = 3; d < 9; d++)
                     {
                        pdata.fe_matadd_ilowers[i][d] =
                           pdata.fe_matadd_ilowers[box][d];
                        pdata.fe_matadd_iuppers[i][d] =
                           pdata.fe_matadd_iuppers[box][d];
                     }
                     pdata.fe_matadd_nrows[i]  = pdata.fe_matadd_nrows[box];
                     pdata.fe_matadd_rows[i]   = pdata.fe_matadd_rows[box];
                     pdata.fe_matadd_ncols[i]  = pdata.fe_matadd_ncols[box];
                     pdata.fe_matadd_cols[i]   = pdata.fe_matadd_cols[box];
                     pdata.fe_matadd_values[i] = pdata.fe_matadd_values[box];
                     i++;
                     break;
                  }
               }
            }
            for (box = i; box < pdata.fe_matadd_nboxes; box++)
            {
               hypre_TFree(pdata.fe_matadd_rows[box]);
               hypre_TFree(pdata.fe_matadd_cols[box]);
               hypre_TFree(pdata.fe_matadd_values[box]);
            }
            pdata.fe_matadd_nboxes = i;
         }

         /* refine and block boxes */
         m[0] = block[part][0];
         m[1] = block[part][1];
         m[2] = block[part][2];
         if ( (m[0] * m[1] * m[2]) > 1)
         {
            pdata.ilowers = hypre_TReAlloc(pdata.ilowers, ProblemIndex,
                                           m[0]*m[1]*m[2]*pdata.nboxes);
            pdata.iuppers = hypre_TReAlloc(pdata.iuppers, ProblemIndex,
                                           m[0]*m[1]*m[2]*pdata.nboxes);
            pdata.boxsizes = hypre_TReAlloc(pdata.boxsizes, int,
                                            m[0]*m[1]*m[2]*pdata.nboxes);
            for (box = 0; box < pdata.nboxes; box++)
            {
               n[0] = pdata.iuppers[box][0] - pdata.ilowers[box][0] + 1;
               n[1] = pdata.iuppers[box][1] - pdata.ilowers[box][1] + 1;
               n[2] = pdata.iuppers[box][2] - pdata.ilowers[box][2] + 1;

               MapProblemIndex(pdata.ilowers[box], m);

               MapProblemIndex(pdata.iuppers[box], m);
               pdata.iuppers[box][0] = pdata.ilowers[box][0] + n[0] - 1;
               pdata.iuppers[box][1] = pdata.ilowers[box][1] + n[1] - 1;
               pdata.iuppers[box][2] = pdata.ilowers[box][2] + n[2] - 1;

               i = box;
               for (r = 0; r < m[2]; r++)
               {
                  for (q = 0; q < m[1]; q++)
                  {
                     for (p = 0; p < m[0]; p++)
                     {
                        pdata.ilowers[i][0] = pdata.ilowers[box][0] + p*n[0];
                        pdata.ilowers[i][1] = pdata.ilowers[box][1] + q*n[1];
                        pdata.ilowers[i][2] = pdata.ilowers[box][2] + r*n[2];
                        pdata.iuppers[i][0] = pdata.iuppers[box][0] + p*n[0];
                        pdata.iuppers[i][1] = pdata.iuppers[box][1] + q*n[1];
                        pdata.iuppers[i][2] = pdata.iuppers[box][2] + r*n[2];
                        for (d = 3; d < 9; d++)
                        {
                           pdata.ilowers[i][d] = pdata.ilowers[box][d];
                           pdata.iuppers[i][d] = pdata.iuppers[box][d];
                        }
                        i += pdata.nboxes;
                     }
                  }
               }
            }
            pdata.nboxes *= m[0]*m[1]*m[2];

            for (box = 0; box < pdata.graph_nboxes; box++)
            {
               MapProblemIndex(pdata.graph_ilowers[box], m);
               MapProblemIndex(pdata.graph_iuppers[box], m);
               mmap[0] = m[pdata.graph_index_maps[box][0]];
               mmap[1] = m[pdata.graph_index_maps[box][1]];
               mmap[2] = m[pdata.graph_index_maps[box][2]];
               MapProblemIndex(pdata.graph_to_ilowers[box], mmap);
               MapProblemIndex(pdata.graph_to_iuppers[box], mmap);
            }
            for (box = 0; box < pdata.matset_nboxes; box++)
            {
               MapProblemIndex(pdata.matset_ilowers[box], m);
               MapProblemIndex(pdata.matset_iuppers[box], m);
            }
            for (box = 0; box < pdata.matadd_nboxes; box++)
            {
               MapProblemIndex(pdata.matadd_ilowers[box], m);
               MapProblemIndex(pdata.matadd_iuppers[box], m);
            }
            for (box = 0; box < pdata.fe_matset_nboxes; box++)
            {
               MapProblemIndex(pdata.fe_matset_ilowers[box], m);
               MapProblemIndex(pdata.fe_matset_iuppers[box], m);
            }
            for (box = 0; box < pdata.fe_matadd_nboxes; box++)
            {
               MapProblemIndex(pdata.fe_matadd_ilowers[box], m);
               MapProblemIndex(pdata.fe_matadd_iuppers[box], m);
            }
         }

         /* map remaining ilowers & iuppers */
         m[0] = refine[part][0] * block[part][0] * distribute[part][0];
         m[1] = refine[part][1] * block[part][1] * distribute[part][1];
         m[2] = refine[part][2] * block[part][2] * distribute[part][2];
         if ( (m[0] * m[1] * m[2]) > 1)
         {
            for (box = 0; box < pdata.glue_nboxes; box++)
            {
               MapProblemIndex(pdata.glue_ilowers[box], m);
               MapProblemIndex(pdata.glue_iuppers[box], m);
               mmap[0] = m[pdata.glue_index_maps[box][0]];
               mmap[1] = m[pdata.glue_index_maps[box][1]];
               mmap[2] = m[pdata.glue_index_maps[box][2]];
               MapProblemIndex(pdata.glue_nbor_ilowers[box], mmap);
               MapProblemIndex(pdata.glue_nbor_iuppers[box], mmap);
            }
         }

         /* compute box sizes, etc. */
         pdata.max_boxsize = 0;
         for (box = 0; box < pdata.nboxes; box++)
         {
            pdata.boxsizes[box] = 1;
            for (i = 0; i < 3; i++)
            {
               pdata.boxsizes[box] *=
                  (pdata.iuppers[box][i] - pdata.ilowers[box][i] + 2);
            }
            pdata.max_boxsize =
               hypre_max(pdata.max_boxsize, pdata.boxsizes[box]);
         }
         for (box = 0; box < pdata.graph_nboxes; box++)
         {
            pdata.graph_boxsizes[box] = 1;
            for (i = 0; i < 3; i++)
            {
               pdata.graph_boxsizes[box] *=
                  (pdata.graph_iuppers[box][i] -
                   pdata.graph_ilowers[box][i] + 1);
            }
         }
         for (box = 0; box < pdata.matset_nboxes; box++)
         {
            size = 1;
            for (i = 0; i < 3; i++)
            {
               size*= (pdata.matset_iuppers[box][i] -
                       pdata.matset_ilowers[box][i] + 1);
            }
            pdata.max_boxsize = hypre_max(pdata.max_boxsize, size);
         }
         for (box = 0; box < pdata.matadd_nboxes; box++)
         {
            size = 1;
            for (i = 0; i < 3; i++)
            {
               size*= (pdata.matadd_iuppers[box][i] -
                       pdata.matadd_ilowers[box][i] + 1);
            }
            pdata.max_boxsize = hypre_max(pdata.max_boxsize, size);
         }
         for (box = 0; box < pdata.fe_matset_nboxes; box++)
         {
            size = 1;
            for (i = 0; i < 3; i++)
            {
               size*= (pdata.fe_matset_iuppers[box][i] -
                       pdata.fe_matset_ilowers[box][i] + 1);
            }
            pdata.max_boxsize = hypre_max(pdata.max_boxsize, size);
         }
         for (box = 0; box < pdata.fe_matadd_nboxes; box++)
         {
            size = 1;
            for (i = 0; i < 3; i++)
            {
               size*= (pdata.fe_matadd_iuppers[box][i] -
                       pdata.fe_matadd_ilowers[box][i] + 1);
            }
            pdata.max_boxsize = hypre_max(pdata.max_boxsize, size);
         }
      }

      if (pdata.nboxes == 0)
      {
         hypre_TFree(pdata.ilowers);
         hypre_TFree(pdata.iuppers);
         hypre_TFree(pdata.boxsizes);
         pdata.max_boxsize = 0;
      }

      if (pdata.glue_nboxes == 0)
      {
         hypre_TFree(pdata.glue_defunct);
         hypre_TFree(pdata.glue_ilowers);
         hypre_TFree(pdata.glue_iuppers);
         hypre_TFree(pdata.glue_nbor_parts);
         hypre_TFree(pdata.glue_nbor_ilowers);
         hypre_TFree(pdata.glue_nbor_iuppers);
         hypre_TFree(pdata.glue_index_maps);
         hypre_TFree(pdata.glue_index_dirs);
         hypre_TFree(pdata.glue_primaries);
      }

      if (pdata.graph_nboxes == 0)
      {
         hypre_TFree(pdata.graph_ilowers);
         hypre_TFree(pdata.graph_iuppers);
         hypre_TFree(pdata.graph_strides);
         hypre_TFree(pdata.graph_vars);
         hypre_TFree(pdata.graph_to_parts);
         hypre_TFree(pdata.graph_to_ilowers);
         hypre_TFree(pdata.graph_to_iuppers);
         hypre_TFree(pdata.graph_to_strides);
         hypre_TFree(pdata.graph_to_vars);
         hypre_TFree(pdata.graph_index_maps);
         hypre_TFree(pdata.graph_index_signs);
         hypre_TFree(pdata.graph_entries);
         hypre_TFree(pdata.graph_values);
         hypre_TFree(pdata.graph_boxsizes);
      }

      if (pdata.matset_nboxes == 0)
      {
         hypre_TFree(pdata.matset_ilowers);
         hypre_TFree(pdata.matset_iuppers);
         hypre_TFree(pdata.matset_strides);
         hypre_TFree(pdata.matset_vars);
         hypre_TFree(pdata.matset_entries);
         hypre_TFree(pdata.matset_values);
      }

      if (pdata.matadd_nboxes == 0)
      {
         hypre_TFree(pdata.matadd_ilowers);
         hypre_TFree(pdata.matadd_iuppers);
         hypre_TFree(pdata.matadd_vars);
         hypre_TFree(pdata.matadd_nentries);
         hypre_TFree(pdata.matadd_entries);
         hypre_TFree(pdata.matadd_values);
      }

      if (pdata.fe_matset_nboxes == 0)
      {
         hypre_TFree(pdata.fe_matset_ilowers);
         hypre_TFree(pdata.fe_matset_iuppers);
         hypre_TFree(pdata.fe_matset_nrows);
         hypre_TFree(pdata.fe_matset_ncols);
         hypre_TFree(pdata.fe_matset_rows);
         hypre_TFree(pdata.fe_matset_cols);
         hypre_TFree(pdata.fe_matset_values);
      }

      if (pdata.fe_matadd_nboxes == 0)
      {
         hypre_TFree(pdata.fe_matadd_ilowers);
         hypre_TFree(pdata.fe_matadd_iuppers);
         hypre_TFree(pdata.fe_matadd_nrows);
         hypre_TFree(pdata.fe_matadd_ncols);
         hypre_TFree(pdata.fe_matadd_rows);
         hypre_TFree(pdata.fe_matadd_cols);
         hypre_TFree(pdata.fe_matadd_values);
      }

      data.pdata[part] = pdata;
   }

   data.max_boxsize = 0;
   for (part = 0; part < data.nparts; part++)
   {
      data.max_boxsize =
         hypre_max(data.max_boxsize, data.pdata[part].max_boxsize);
   }

   hypre_TFree(pool_procs);

   *data_ptr = data; 
   return 0;
}

/*--------------------------------------------------------------------------
 * Destroy data
 *--------------------------------------------------------------------------*/

int
DestroyData( ProblemData   data )
{
   ProblemPartData  pdata;
   int              part, box, s;

   for (part = 0; part < data.nparts; part++)
   {
      pdata = data.pdata[part];

      if (pdata.nboxes > 0)
      {
         hypre_TFree(pdata.ilowers);
         hypre_TFree(pdata.iuppers);
         hypre_TFree(pdata.boxsizes);
      }

      if (pdata.nvars > 0)
      {
         hypre_TFree(pdata.vartypes);
      }

      if (pdata.add_nvars > 0)
      {
         hypre_TFree(pdata.add_indexes);
         hypre_TFree(pdata.add_vartypes);
      }

      if (pdata.glue_nboxes > 0)
      {
         hypre_TFree(pdata.glue_defunct);
         hypre_TFree(pdata.glue_ilowers);
         hypre_TFree(pdata.glue_iuppers);
         hypre_TFree(pdata.glue_nbor_parts);
         hypre_TFree(pdata.glue_nbor_ilowers);
         hypre_TFree(pdata.glue_nbor_iuppers);
         hypre_TFree(pdata.glue_index_maps);
         hypre_TFree(pdata.glue_index_dirs);
         hypre_TFree(pdata.glue_primaries);
      }

      if (pdata.nvars > 0)
      {
         hypre_TFree(pdata.stencil_num);
      }

      if (pdata.graph_nboxes > 0)
      {
         hypre_TFree(pdata.graph_ilowers);
         hypre_TFree(pdata.graph_iuppers);
         hypre_TFree(pdata.graph_strides);
         hypre_TFree(pdata.graph_vars);
         hypre_TFree(pdata.graph_to_parts);
         hypre_TFree(pdata.graph_to_ilowers);
         hypre_TFree(pdata.graph_to_iuppers);
         hypre_TFree(pdata.graph_to_strides);
         hypre_TFree(pdata.graph_to_vars);
         hypre_TFree(pdata.graph_index_maps);
         hypre_TFree(pdata.graph_index_signs);
         hypre_TFree(pdata.graph_entries);
         hypre_TFree(pdata.graph_values);
         hypre_TFree(pdata.graph_boxsizes);
      }

      if (pdata.matset_nboxes > 0)
      {
         hypre_TFree(pdata.matset_ilowers);
         hypre_TFree(pdata.matset_iuppers);
         hypre_TFree(pdata.matset_strides);
         hypre_TFree(pdata.matset_vars);
         hypre_TFree(pdata.matset_entries);
         hypre_TFree(pdata.matset_values);
      }

      if (pdata.matadd_nboxes > 0)
      {
         hypre_TFree(pdata.matadd_ilowers);
         hypre_TFree(pdata.matadd_iuppers);
         hypre_TFree(pdata.matadd_vars);
         hypre_TFree(pdata.matadd_nentries);
         for (box = 0; box < pdata.matadd_nboxes; box++)
         {
            hypre_TFree(pdata.matadd_entries[box]);
            hypre_TFree(pdata.matadd_values[box]);
         }
         hypre_TFree(pdata.matadd_entries);
         hypre_TFree(pdata.matadd_values);
      }

      if (pdata.fe_matset_nboxes > 0)
      {
         hypre_TFree(pdata.fe_matset_ilowers);
         hypre_TFree(pdata.fe_matset_iuppers);
         hypre_TFree(pdata.fe_matset_nrows);
         hypre_TFree(pdata.fe_matset_ncols);
         for (box = 0; box < pdata.fe_matset_nboxes; box++)
         {
            hypre_TFree(pdata.fe_matset_rows[box]);
            hypre_TFree(pdata.fe_matset_cols[box]);
            hypre_TFree(pdata.fe_matset_values[box]);
         }
         hypre_TFree(pdata.fe_matset_rows);
         hypre_TFree(pdata.fe_matset_cols);
         hypre_TFree(pdata.fe_matset_values);
      }

      if (pdata.fe_matadd_nboxes > 0)
      {
         hypre_TFree(pdata.fe_matadd_ilowers);
         hypre_TFree(pdata.fe_matadd_iuppers);
         hypre_TFree(pdata.fe_matadd_nrows);
         hypre_TFree(pdata.fe_matadd_ncols);
         for (box = 0; box < pdata.fe_matadd_nboxes; box++)
         {
            hypre_TFree(pdata.fe_matadd_rows[box]);
            hypre_TFree(pdata.fe_matadd_cols[box]);
            hypre_TFree(pdata.fe_matadd_values[box]);
         }
         hypre_TFree(pdata.fe_matadd_rows);
         hypre_TFree(pdata.fe_matadd_cols);
         hypre_TFree(pdata.fe_matadd_values);
      }
   }
   hypre_TFree(data.pdata);

   for (s = 0; s < data.nstencils; s++)
   {
      hypre_TFree(data.stencil_offsets[s]);
      hypre_TFree(data.stencil_vars[s]);
      hypre_TFree(data.stencil_values[s]);
   }
   hypre_TFree(data.stencil_sizes);
   hypre_TFree(data.stencil_offsets);
   hypre_TFree(data.stencil_vars);
   hypre_TFree(data.stencil_values);

   if (data.fe_stencil_size > 0)
   {
      for (s = 0; s < data.fe_stencil_size; s++)
      {
         hypre_TFree(data.fe_stencil_values[s]);
         hypre_TFree(data.fe_stencil_entries[s]);
      }
      hypre_TFree(data.fe_stencil_offsets);
      hypre_TFree(data.fe_stencil_vars);
      hypre_TFree(data.fe_stencil_values);
      hypre_TFree(data.fe_stencil_entries);
   }

   if (data.symmetric_num > 0)
   {
      hypre_TFree(data.symmetric_parts);
      hypre_TFree(data.symmetric_vars);
      hypre_TFree(data.symmetric_to_vars);
      hypre_TFree(data.symmetric_booleans);
   }

   hypre_TFree(data.pools);

   return 0;
}

/*--------------------------------------------------------------------------
 * Routine to load cosine function
 *--------------------------------------------------------------------------*/

int
SetCosineVector(   double  scale,
                   Index   ilower,
                   Index   iupper,
                   double *values)
{
   int          i, j, k;
   int          count = 0;

   for (k = ilower[2]; k <= iupper[2]; k++)
   {
      for (j = ilower[1]; j <= iupper[1]; j++)
      {
         for (i = ilower[0]; i <= iupper[0]; i++)
         {
            values[count] = scale * cos((i+j+k)/10.0);
            count++;
         }
      }
   }

   return(0);
}

/*--------------------------------------------------------------------------
 * Print usage info
 *--------------------------------------------------------------------------*/

int
PrintUsage( char *progname,
            int   myid )
{
   if ( myid == 0 )
   {
      printf("\n");
      printf("Usage: %s [<options>]\n", progname);
      printf("\n");
      printf("  -in <filename> : input file (default is `%s')\n",
             infile_default);
      printf("\n");
      printf("  -pt <pt1> <pt2> ... : set part(s) for subsequent options\n");
      printf("  -r <rx> <ry> <rz>   : refine part(s)\n");
      printf("  -P <Px> <Py> <Pz>   : refine and distribute part(s)\n");
      printf("  -b <bx> <by> <bz>   : refine and block part(s)\n");
      printf("  -solver <ID>        : solver ID (default = 39)\n");
      printf("                         0 - SMG split solver\n");
      printf("                         1 - PFMG split solver\n");
      printf("                         3 - SysPFMG\n");
      printf("                         8 - 1-step Jacobi split solver\n");
      printf("                        10 - PCG with SMG split precond\n");
      printf("                        11 - PCG with PFMG split precond\n");
      printf("                        13 - PCG with SysPFMG precond\n");
      printf("                        18 - PCG with diagonal scaling\n");
      printf("                        19 - PCG\n");
      printf("                        20 - PCG with BoomerAMG precond\n");
      printf("                        22 - PCG with ParaSails precond\n");
      printf("                        28 - PCG with diagonal scaling\n");
      printf("                        30 - GMRES with SMG split precond\n");
      printf("                        31 - GMRES with PFMG split precond\n");
      printf("                        38 - GMRES with diagonal scaling\n");
      printf("                        39 - GMRES\n");
      printf("                        40 - GMRES with BoomerAMG precond\n");
      printf("                        41 - GMRES with EUCLID precond\n");
      printf("                        42 - GMRES with ParaSails precond\n");
      printf("                        50 - BiCGSTAB with SMG split precond\n");
      printf("                        51 - BiCGSTAB with PFMG split precond\n");
      printf("                        58 - BiCGSTAB with diagonal scaling\n");
      printf("                        59 - BiCGSTAB\n");
      printf("                        60 - BiCGSTAB with BoomerAMG precond\n");
      printf("                        61 - BiCGSTAB with EUCLID precond\n");
      printf("                        62 - BiCGSTAB with ParaSails precond\n");

      printf("                        70 - Flexible GMRES with SMG split precond\n");
      printf("                        71 - Flexible GMRES with PFMG split precond\n");
      printf("                        78 - Flexible GMRES with diagonal scaling\n");
      printf("                        80 - Flexible GMRES with BoomerAMG precond\n");

      printf("                        90 - LGMRES with BoomerAMG precond\n");


      printf("                        120- PCG with hybrid precond\n");
      printf("                        200- Struct SMG\n");
      printf("                        201- Struct PFMG\n");
      printf("                        202- Struct SparseMSG\n");
      printf("                        203- Struct PFMG constant coefficients\n");
      printf("                        204- Struct PFMG constant coefficients variable diagonal\n");
      printf("                        208- Struct Jacobi\n");
      printf("                        210- Struct CG with SMG precond\n");
      printf("                        211- Struct CG with PFMG precond\n");
      printf("                        212- Struct CG with SparseMSG precond\n");
      printf("                        217- Struct CG with 2-step Jacobi\n");
      printf("                        218- Struct CG with diagonal scaling\n");
      printf("                        219- Struct CG\n");
      printf("                        220- Struct Hybrid with SMG precond\n");
      printf("                        221- Struct Hybrid with PFMG precond\n");
      printf("                        222- Struct Hybrid with SparseMSG precond\n");
      printf("                        230- Struct GMRES with SMG precond\n");
      printf("                        231- Struct GMRES with PFMG precond\n");
      printf("                        232- Struct GMRES with SparseMSG precond\n");
      printf("                        237- Struct GMRES with 2-step Jacobi\n");
      printf("                        238- Struct GMRES with diagonal scaling\n");
      printf("                        239- Struct GMRES\n");
      printf("                        240- Struct BiCGSTAB with SMG precond\n");
      printf("                        241- Struct BiCGSTAB with PFMG precond\n");
      printf("                        242- Struct BiCGSTAB with SparseMSG precond\n");
      printf("                        247- Struct BiCGSTAB with 2-step Jacobi\n");
      printf("                        248- Struct BiCGSTAB with diagonal scaling\n");
      printf("                        249- Struct BiCGSTAB\n");
      printf("  -print             : print out the system\n");
      printf("  -rhsfromcosine     : solution is cosine function (default)\n");
      printf("  -rhsone            : rhs is vector with unit components\n");
      printf("  -v <n_pre> <n_post>: SysPFMG and Struct- # of pre and post relax\n");
      printf("  -skip <s>          : SysPFMG and Struct- skip relaxation (0 or 1)\n");
      printf("  -rap <r>           : Struct- coarse grid operator type\n");
      printf("                        0 - Galerkin (default)\n");
      printf("                        1 - non-Galerkin ParFlow operators\n");
      printf("                        2 - Galerkin, general operators\n");
      printf("  -relax <r>         : Struct- relaxation type\n");
      printf("                        0 - Jacobi\n");
      printf("                        1 - Weighted Jacobi (default)\n");
      printf("                        2 - R/B Gauss-Seidel\n");
      printf("                        3 - R/B Gauss-Seidel (nonsymmetric)\n");
      printf("  -w <jacobi_weight> : jacobi weight\n");
      printf("  -sym <s>           : Struct- symmetric storage (1) or not (0)\n");
      printf("  -jump <num>        : Struct- num levels to jump in SparseMSG\n");
      printf("  -solver_type <ID>  : Struct- solver type for Hybrid\n");
      printf("                        1 - PCG (default)\n");
      printf("                        2 - GMRES\n");
      printf("  -cf <cf>           : Struct- convergence factor for Hybrid\n");

      /* begin lobpcg */

      printf("\nLOBPCG options:\n");
      printf("\n");
      printf("  -lobpcg            : run LOBPCG instead of PCG\n");
      printf("\n");
      printf("  -solver none       : no HYPRE preconditioner is used\n");
      printf("\n");
      printf("  -itr <val>         : maximal number of LOBPCG iterations (default 100);\n");
      printf("\n");
      printf("  -tol <val>         : residual tolerance (default 1e-6)\n");
      printf("\n");
      printf("  -vrand <val>       : compute <val> eigenpairs using random initial vectors (default 1)\n");
      printf("\n");
      printf("  -seed <val>        : use <val> as the seed for the pseudo-random number generator\n"); 
      printf("                       (default seed is based on the time of the run)\n");
      printf("\n");
      printf("  -orthchk           : check eigenvectors for orthonormality\n");
      printf("\n");
      printf("  -verb <val>        : verbosity level\n");
      printf("  -verb 0            : no print\n");
      printf("  -verb 1            : print initial eigenvalues and residuals, iteration number, number of\n");
      printf("                       non-convergent eigenpairs and final eigenvalues and residuals (default)\n");
      printf("  -verb 2            : print eigenvalues and residuals on each iteration\n");
      printf("\n");
      printf("  -pcgitr <val>      : maximal number of inner PCG iterations for preconditioning (default 1);\n");
      printf("                       if <val> = 0 then the preconditioner is applied directly\n");
      printf("\n");
      printf("  -pcgtol <val>      : residual tolerance for inner iterations (default 0.01)\n");
      printf("\n");
      printf("  -vout <val>        : file output level\n");
      printf("  -vout 0            : no files created (default)\n");
      printf("  -vout 1            : write eigenvalues to values.txt and residuals to residuals.txt\n");
      printf("  -vout 2            : in addition to the above, write the eigenvalues history (the matrix whose\n");
      printf("                       i-th column contains eigenvalues at (i+1)-th iteration) to val_hist.txt and\n");
      printf("                       residuals history to res_hist.txt\n");
      printf("\nNOTE: in this test driver LOBPCG only works with solvers 10, 11, 13, and 18\n");
      printf("\ndefault solver is 10\n");
      
      /* end lobpcg */

      printf("\n");
   }

   return 0;
}

/*--------------------------------------------------------------------------
 * Test driver for semi-structured matrix interface
 *--------------------------------------------------------------------------*/
 
int
main( int   argc,
      char *argv[] )
{
   char                 *infile;
   ProblemData           global_data;
   ProblemData           data;
   ProblemPartData       pdata;
   int                   nparts;
   int                  *parts;
   Index                *refine;
   Index                *distribute;
   Index                *block;
   int                   solver_id, object_type;
   int                   print_system;
   int                   cosine;
   double                scale;
                        
   HYPRE_SStructGrid     grid;
   HYPRE_SStructStencil *stencils;
   HYPRE_SStructGraph    graph;
   HYPRE_SStructMatrix   A;
   HYPRE_SStructVector   b;
   HYPRE_SStructVector   x;
   HYPRE_SStructSolver   solver;
   HYPRE_SStructSolver   precond;

   HYPRE_ParCSRMatrix    par_A;
   HYPRE_ParVector       par_b;
   HYPRE_ParVector       par_x;
   HYPRE_Solver          par_solver;
   HYPRE_Solver          par_precond;

   HYPRE_StructMatrix    sA;
   HYPRE_StructVector    sb;
   HYPRE_StructVector    sx;
   HYPRE_StructSolver    struct_solver;
   HYPRE_StructSolver    struct_precond;

   Index                 ilower, iupper;
   Index                 index, to_index;
   double               *values;

   int                   num_iterations;
   double                final_res_norm;
                         
   int                   num_procs, myid;
   int                   time_index;

   int                   n_pre, n_post;
   int                   skip;
   int                   sym;
   int                   rap;
   int                   relax;
   double                jacobi_weight;
   int                   usr_jacobi_weight;
   int                   jump;
   int                   solver_type;

   double                cf_tol;

   int                   arg_index, part, var, box, s, entry, i, j, k, size;
   int                   row, col;
                        
   /* begin lobpcg */

   HYPRE_SStructSolver   lobpcg_solver;

   int lobpcgFlag = 0;
   int lobpcgSeed = 0;
   int blockSize = 1;
   int verbosity = 1;
   int iterations;
   int maxIterations = 100;
   int checkOrtho = 0;
   int printLevel = 0;
   int pcgIterations = 0;
   int pcgMode = 0;
   double tol = 1e-6;
   double pcgTol = 1e-2;
   double nonOrthF;

   FILE* filePtr;

   mv_MultiVectorPtr eigenvectors = NULL;
   mv_MultiVectorPtr constrains = NULL;
   double* eigenvalues = NULL;

   double* residuals;
   utilities_FortranMatrix* residualNorms;
   utilities_FortranMatrix* residualNormsHistory;
   utilities_FortranMatrix* eigenvaluesHistory;
   utilities_FortranMatrix* printBuffer;
   utilities_FortranMatrix* gramXX;
   utilities_FortranMatrix* identity;

   mv_InterfaceInterpreter* interpreter;
   HYPRE_MatvecFunctions matvec_fn;

   /* end lobpcg */

   /*-----------------------------------------------------------
    * Initialize some stuff
    *-----------------------------------------------------------*/

   /* Initialize MPI */
   MPI_Init(&argc, &argv);

   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   hypre_InitMemoryDebug(myid);

   /*-----------------------------------------------------------
    * Read input file
    *-----------------------------------------------------------*/

   arg_index = 1;

   /* parse command line for input file name */
   infile = infile_default;
   if (argc > 1)
   {
      if ( strcmp(argv[arg_index], "-in") == 0 )
      {
         arg_index++;
         infile = argv[arg_index++];
      }
      else if ( strcmp(argv[arg_index], "-help") == 0 )
      {
         PrintUsage(argv[0], myid);
         exit(1);
      }
   }

   ReadData(infile, &global_data);

   /*-----------------------------------------------------------
    * Set defaults
    *-----------------------------------------------------------*/

   skip  = 0;
   sym   = 1;
   rap   = 0;
   relax = 1;
   usr_jacobi_weight= 0;
   jump  = 0;
   solver_type = 1;
   cf_tol = 0.90;

   nparts = global_data.nparts;

   parts      = hypre_TAlloc(int, nparts);
   refine     = hypre_TAlloc(Index, nparts);
   distribute = hypre_TAlloc(Index, nparts);
   block      = hypre_TAlloc(Index, nparts);
   for (part = 0; part < nparts; part++)
   {
      parts[part] = part;
      for (j = 0; j < 3; j++)
      {
         refine[part][j]     = 1;
         distribute[part][j] = 1;
         block[part][j]      = 1;
      }
   }

   solver_id = 39;
   print_system = 0;
   cosine = 1;

   skip = 0;
   n_pre  = 1;
   n_post = 1;

   /*-----------------------------------------------------------
    * Parse command line
    *-----------------------------------------------------------*/

   while (arg_index < argc)
   {
      if ( strcmp(argv[arg_index], "-pt") == 0 )
      {
         arg_index++;
         nparts = 0;
         while ( strncmp(argv[arg_index], "-", 1) != 0 )
         {
            parts[nparts++] = atoi(argv[arg_index++]);
         }
      }
      else if ( strcmp(argv[arg_index], "-r") == 0 )
      {
         arg_index++;
         for (i = 0; i < nparts; i++)
         {
            part = parts[i];
            k = arg_index;
            for (j = 0; j < 3; j++)
            {
               refine[part][j] = atoi(argv[k++]);
            }
         }
         arg_index += 3;
      }
      else if ( strcmp(argv[arg_index], "-P") == 0 )
      {
         arg_index++;
         for (i = 0; i < nparts; i++)
         {
            part = parts[i];
            k = arg_index;
            for (j = 0; j < 3; j++)
            {
               distribute[part][j] = atoi(argv[k++]);
            }
         }
         arg_index += 3;
      }
      else if ( strcmp(argv[arg_index], "-b") == 0 )
      {
         arg_index++;
         for (i = 0; i < nparts; i++)
         {
            part = parts[i];
            k = arg_index;
            for (j = 0; j < 3; j++)
            {
               block[part][j] = atoi(argv[k++]);
            }
         }
         arg_index += 3;
      }
      else if ( strcmp(argv[arg_index], "-solver") == 0 )
      {
         arg_index++;

	 /* begin lobpcg */
	 if ( strcmp(argv[arg_index], "none") == 0 ) {
	   solver_id = NO_SOLVER;
	   arg_index++;
	 }
	 else /* end lobpcg */
	   solver_id = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-rhsone") == 0 )
      {
         arg_index++;
         cosine = 0;
      }
      else if ( strcmp(argv[arg_index], "-rhsfromcosine") == 0 )
      {
         arg_index++;
         cosine = 1;
      }
      else if ( strcmp(argv[arg_index], "-print") == 0 )
      {
         arg_index++;
         print_system = 1;
      }
      else if ( strcmp(argv[arg_index], "-skip") == 0 )
      {
         arg_index++;
         skip = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-v") == 0 )
      {
         arg_index++;
         n_pre = atoi(argv[arg_index++]);
         n_post = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-rap") == 0 )
      {
         arg_index++;
         rap = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-relax") == 0 )
      {
         arg_index++;
         relax = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-w") == 0 )
      {
         arg_index++;
         jacobi_weight= atof(argv[arg_index++]);
         usr_jacobi_weight= 1; /* flag user weight */
      }
      else if ( strcmp(argv[arg_index], "-sym") == 0 )
      {
         arg_index++;
         sym = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-jump") == 0 )
      {
         arg_index++;
         jump = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-solver_type") == 0 )
      {
         arg_index++;
         solver_type = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-cf") == 0 )
      {
         arg_index++;
         cf_tol = atof(argv[arg_index++]);
      }
      /* begin lobpcg */
      else if ( strcmp(argv[arg_index], "-lobpcg") == 0 ) 
      {					 /* use lobpcg */
         arg_index++;
		 lobpcgFlag = 1;
      }
      else if ( strcmp(argv[arg_index], "-orthchk") == 0 )
      {			/* lobpcg: check orthonormality */
         arg_index++;
	 checkOrtho = 1;
      }
      else if ( strcmp(argv[arg_index], "-verb") == 0 ) 
      {			  /* lobpcg: verbosity level */
         arg_index++;
         verbosity = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-vrand") == 0 )
      {                         /* lobpcg: block size */
         arg_index++;
         blockSize = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-seed") == 0 )
      {		           /* lobpcg: seed for srand */
         arg_index++;
         lobpcgSeed = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-itr") == 0 ) 
      {		     /* lobpcg: max # of iterations */
         arg_index++;
         maxIterations = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-tol") == 0 ) 
      {			       /* lobpcg: tolerance */
         arg_index++;
         tol = atof(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-pcgitr") == 0 ) 
      {		   /* lobpcg: max inner pcg iterations */
         arg_index++;
         pcgIterations = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-pcgtol") == 0 ) 
      {	     /* lobpcg: inner pcg iterations tolerance */
         arg_index++;
         pcgTol = atof(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-pcgmode") == 0 ) 
      {		 /* lobpcg: initial guess for inner pcg */
         arg_index++;	      /* 0: zero, otherwise rhs */
         pcgMode = atoi(argv[arg_index++]);
      }
      else if ( strcmp(argv[arg_index], "-vout") == 0 )
      {			      /* lobpcg: print level */
         arg_index++;
         printLevel = atoi(argv[arg_index++]);
      }
      else
      {
	arg_index++;
	/*break;*/
      }
   }

   if ( solver_id == 39 && lobpcgFlag )
     solver_id = 10;

   /* end lobpcg */

   /*-----------------------------------------------------------
    * Print driver parameters TODO
    *-----------------------------------------------------------*/
   if (myid == 0)
   {
   }

   /*-----------------------------------------------------------
    * Distribute data
    *-----------------------------------------------------------*/

   DistributeData(global_data, refine, distribute, block,
                  num_procs, myid, &data);

   /*-----------------------------------------------------------
    * Check a few things
    *-----------------------------------------------------------*/
   if (solver_id >= 200)
   {
      pdata = data.pdata[0];
      if (nparts > 1)
      {
         if (!myid)
         {
            printf("Warning: Invalid number of parts for Struct Solver. Part 0 taken. \n");
         }
      }

      if (pdata.nvars > 1)
      {
         if (!myid)
         {
            printf("Error: Invalid number of nvars for Struct Solver \n");
         }
         exit(1);
      }
   }

   /*-----------------------------------------------------------
    * Synchronize so that timings make sense
    *-----------------------------------------------------------*/

   MPI_Barrier(MPI_COMM_WORLD);

   /*-----------------------------------------------------------
    * Set up the grid
    *-----------------------------------------------------------*/

   time_index = hypre_InitializeTiming("SStruct Interface");
   hypre_BeginTiming(time_index);

   HYPRE_SStructGridCreate(MPI_COMM_WORLD, data.ndim, data.nparts, &grid);
   for (part = 0; part < data.nparts; part++)
   {
      pdata = data.pdata[part];
      for (box = 0; box < pdata.nboxes; box++)
      {
         HYPRE_SStructGridSetExtents(grid, part,
                                     pdata.ilowers[box], pdata.iuppers[box]);
      }

      HYPRE_SStructGridSetVariables(grid, part, pdata.nvars, pdata.vartypes);

      /* GridAddVariabes */

      /* GridSetNeighborPart (and defunct GridSetNeighborBox) */
      for (box = 0; box < pdata.glue_nboxes; box++)
      {
         if (!pdata.glue_defunct[box])
         {
            HYPRE_SStructGridSetNeighborPart(grid, part,
                                             pdata.glue_ilowers[box],
                                             pdata.glue_iuppers[box],
                                             pdata.glue_nbor_parts[box],
                                             pdata.glue_nbor_ilowers[box],
                                             pdata.glue_nbor_iuppers[box],
                                             pdata.glue_index_maps[box],
                                             pdata.glue_index_dirs[box]);
         }
         else
         {
            HYPRE_SStructGridSetNeighborBox(grid, part,
                                            pdata.glue_ilowers[box],
                                            pdata.glue_iuppers[box],
                                            pdata.glue_nbor_parts[box],
                                            pdata.glue_nbor_ilowers[box],
                                            pdata.glue_nbor_iuppers[box],
                                            pdata.glue_index_maps[box]);
         }
      }

      HYPRE_SStructGridSetPeriodic(grid, part, pdata.periodic);
   }
   HYPRE_SStructGridAssemble(grid);

   /*-----------------------------------------------------------
    * Set up the stencils
    *-----------------------------------------------------------*/

   stencils = hypre_CTAlloc(HYPRE_SStructStencil, data.nstencils);
   for (s = 0; s < data.nstencils; s++)
   {
      HYPRE_SStructStencilCreate(data.ndim, data.stencil_sizes[s],
                                 &stencils[s]);
      for (entry = 0; entry < data.stencil_sizes[s]; entry++)
      {
         HYPRE_SStructStencilSetEntry(stencils[s], entry,
                                      data.stencil_offsets[s][entry],
                                      data.stencil_vars[s][entry]);
      }
   }

   /*-----------------------------------------------------------
    * Set object type
    *-----------------------------------------------------------*/

   object_type = HYPRE_SSTRUCT;

   if ( ((solver_id >= 20) && (solver_id < 30)) ||
        ((solver_id >= 40) && (solver_id < 50)) ||
        ((solver_id >= 60) && (solver_id < 70)) ||
        ((solver_id >= 80) && (solver_id < 90)) ||
        ((solver_id >= 90) && (solver_id < 100)) ||
        (solver_id == 120))
   {
       object_type = HYPRE_PARCSR;  
   }

   if (solver_id >= 200)
   {
       object_type = HYPRE_STRUCT;
   }

   /*-----------------------------------------------------------
    * Set up the graph
    *-----------------------------------------------------------*/

   HYPRE_SStructGraphCreate(MPI_COMM_WORLD, grid, &graph);

   /* HYPRE_SSTRUCT is the default, so we don't have to call SetObjectType */
   if ( object_type != HYPRE_SSTRUCT )
   {
       HYPRE_SStructGraphSetObjectType(graph, object_type);
   }

   for (part = 0; part < data.nparts; part++)
   {
      pdata = data.pdata[part];

      /* set stencils */
      for (var = 0; var < pdata.nvars; var++)
      {
         HYPRE_SStructGraphSetStencil(graph, part, var,
                                      stencils[pdata.stencil_num[var]]);
      }

      /* add entries */
      for (box = 0; box < pdata.graph_nboxes; box++)
      {
         for (index[2] = pdata.graph_ilowers[box][2];
              index[2] <= pdata.graph_iuppers[box][2];
              index[2] += pdata.graph_strides[box][2])
         {
            for (index[1] = pdata.graph_ilowers[box][1];
                 index[1] <= pdata.graph_iuppers[box][1];
                 index[1] += pdata.graph_strides[box][1])
            {
               for (index[0] = pdata.graph_ilowers[box][0];
                    index[0] <= pdata.graph_iuppers[box][0];
                    index[0] += pdata.graph_strides[box][0])
               {
                  for (i = 0; i < 3; i++)
                  {
                     j = pdata.graph_index_maps[box][i];
                     k = index[i] - pdata.graph_ilowers[box][i];
                     k /= pdata.graph_strides[box][i];
                     k *= pdata.graph_index_signs[box][i];
                     to_index[j] = pdata.graph_to_ilowers[box][j] +
                        k * pdata.graph_to_strides[box][j];
                  }
                  HYPRE_SStructGraphAddEntries(graph, part, index,
                                               pdata.graph_vars[box],
                                               pdata.graph_to_parts[box],
                                               to_index,
                                               pdata.graph_to_vars[box]);
               }
            }
         }
      }
   }

   HYPRE_SStructGraphAssemble(graph);

   /*-----------------------------------------------------------
    * Set up the matrix
    *-----------------------------------------------------------*/

   values = hypre_TAlloc(double, data.max_boxsize);

   HYPRE_SStructMatrixCreate(MPI_COMM_WORLD, graph, &A);

   /* TODO HYPRE_SStructMatrixSetSymmetric(A, 1); */
   for (i = 0; i < data.symmetric_num; i++)
   {
      HYPRE_SStructMatrixSetSymmetric(A, data.symmetric_parts[i],
                                      data.symmetric_vars[i],
                                      data.symmetric_to_vars[i],
                                      data.symmetric_booleans[i]);
   }
   HYPRE_SStructMatrixSetNSSymmetric(A, data.ns_symmetric);

   /* HYPRE_SSTRUCT is the default, so we don't have to call SetObjectType */
   if ( object_type != HYPRE_SSTRUCT )
   {
       HYPRE_SStructMatrixSetObjectType(A, object_type);
   }

   HYPRE_SStructMatrixInitialize(A);

   for (part = 0; part < data.nparts; part++)
   {
      pdata = data.pdata[part];

      /* StencilSetEntry: set stencil values */
      for (var = 0; var < pdata.nvars; var++)
      {
         s = pdata.stencil_num[var];
         for (i = 0; i < data.stencil_sizes[s]; i++)
         {
            for (j = 0; j < pdata.max_boxsize; j++)
            {
               values[j] = data.stencil_values[s][i];
            }
            for (box = 0; box < pdata.nboxes; box++)
            {
               GetVariableBox(pdata.ilowers[box], pdata.iuppers[box],
                              pdata.vartypes[var], ilower, iupper);
               HYPRE_SStructMatrixSetBoxValues(A, part, ilower, iupper,
                                               var, 1, &i, values);
            }
         }
      }

      /* FEStencilSetRow: add to stencil values */
      for (i = 0; i < data.fe_stencil_size; i++)
      {
         s = data.fe_stencil_vars[i];

         for (j = 0; j < data.fe_stencil_size; j++)
         {
            var = data.fe_stencil_vars[i];
            entry = data.fe_stencil_entries[i][j];
            /* only set valid stencil entries */
            if (entry < 0)
            {
               continue;
            }
            for (k = 0; k < pdata.max_boxsize; k++)
            {
               values[k] = data.fe_stencil_values[i][j];
            }
            for (box = 0; box < pdata.nboxes; box++)
            {
               for (k = 0; k < 3; k++)
               {
                  ilower[k] = pdata.ilowers[box][k] + data.fe_stencil_offsets[i][k];
                  iupper[k] = pdata.iuppers[box][k] + data.fe_stencil_offsets[i][k];
               }
               HYPRE_SStructMatrixAddToBoxValues(A, part, ilower, iupper,
                                                 var, 1, &entry, values);
            }
         }
      }

      /* GraphAddEntries: set non-stencil entries */
      for (box = 0; box < pdata.graph_nboxes; box++)
      {
         /*
          * RDF NOTE: Add a separate interface routine for setting non-stencil
          * entries.  It would be more efficient to set boundary values a box
          * at a time, but AMR may require striding, and some codes may already
          * have a natural values array to pass in, but can't because it uses
          * ghost values.
          *
          * Example new interface routine:
          *   SetNSBoxValues(matrix, part, ilower, iupper, stride, entry
          *                  values_ilower, values_iupper, values);
          */

/* since we have already tested SetBoxValues above, use SetValues here */
#if 0
         for (j = 0; j < pdata.graph_boxsizes[box]; j++)
         {
            values[j] = pdata.graph_values[box];
         }
         HYPRE_SStructMatrixSetBoxValues(A, part,
                                         pdata.graph_ilowers[box],
                                         pdata.graph_iuppers[box],
                                         pdata.graph_vars[box],
                                         1, &pdata.graph_entries[box],
                                         values);
#else
         for (index[2] = pdata.graph_ilowers[box][2];
              index[2] <= pdata.graph_iuppers[box][2];
              index[2] += pdata.graph_strides[box][2])
         {
            for (index[1] = pdata.graph_ilowers[box][1];
                 index[1] <= pdata.graph_iuppers[box][1];
                 index[1] += pdata.graph_strides[box][1])
            {
               for (index[0] = pdata.graph_ilowers[box][0];
                    index[0] <= pdata.graph_iuppers[box][0];
                    index[0] += pdata.graph_strides[box][0])
               {
                  HYPRE_SStructMatrixSetValues(A, part, index,
                                               pdata.graph_vars[box],
                                               1, &pdata.graph_entries[box],
                                               &pdata.graph_values[box]);
               }
            }
         }
#endif
      }

      /* MatrixSetValues: reset some matrix values */
      for (box = 0; box < pdata.matset_nboxes; box++)
      {
         size= 1;
         for (j = 0; j < 3; j++)
         {
            size*= (pdata.matset_iuppers[box][j] -
                    pdata.matset_ilowers[box][j] + 1);
         }
         for (j = 0; j < size; j++)
         {
            values[j] = pdata.matset_values[box];
         }
         HYPRE_SStructMatrixSetBoxValues(A, part,
                                         pdata.matset_ilowers[box],
                                         pdata.matset_iuppers[box],
                                         pdata.matset_vars[box],
                                         1, &pdata.matset_entries[box],
                                         values);
      }

      /* MatrixAddToValues: add to some matrix values */
      for (box = 0; box < pdata.matadd_nboxes; box++)
      {
         size = 1;
         for (j = 0; j < 3; j++)
         {
            size*= (pdata.matadd_iuppers[box][j] -
                    pdata.matadd_ilowers[box][j] + 1);
         }

         for (entry = 0; entry < pdata.matadd_nentries[box]; entry++)
         {
            for (j = 0; j < size; j++)
            {
               values[j] = pdata.matadd_values[box][entry];
            }
          
            HYPRE_SStructMatrixAddToBoxValues(A, part, 
                                              pdata.matadd_ilowers[box],
                                              pdata.matadd_iuppers[box],
                                              pdata.matadd_vars[box],
                                              1, &pdata.matadd_entries[box][entry],
                                              values);
         }
      }

      /* FEMatrixSetValues: set some matrix values */
      for (box = 0; box < pdata.fe_matset_nboxes; box++)
      {
         size = 1;
         for (k = 0; k < 3; k++)
         {
            size*= (pdata.fe_matset_iuppers[box][k] -
                    pdata.fe_matset_ilowers[box][k] + 1);
         }

         s = 0;
         for (i = 0; i < pdata.fe_matset_nrows[box]; i++)
         {
            row = pdata.fe_matset_rows[box][i];

            for (j = 0; j < pdata.fe_matset_ncols[box]; j++)
            {
               col = pdata.fe_matset_cols[box][j];

               for (k = 0; k < 3; k++)
               {
                  ilower[k] = pdata.fe_matset_ilowers[box][k] +
                     data.fe_stencil_offsets[row][k];
                  iupper[k] = pdata.fe_matset_iuppers[box][k] +
                     data.fe_stencil_offsets[row][k];
               }

               for (k = 0; k < size; k++)
               {
                  values[k] = pdata.fe_matset_values[box][s];
               }
               s++;
               
               var = data.fe_stencil_vars[row];
               entry = data.fe_stencil_entries[row][col];
               if (entry > -1)
               {
                  HYPRE_SStructMatrixSetBoxValues(A, part, ilower, iupper,
                                                  var, 1, &entry, values);
               }
            }
         }
      }

      /* FEMatrixAddToValues: add to some matrix values */
      for (box = 0; box < pdata.fe_matadd_nboxes; box++)
      {
         size = 1;
         for (k = 0; k < 3; k++)
         {
            size*= (pdata.fe_matadd_iuppers[box][k] -
                    pdata.fe_matadd_ilowers[box][k] + 1);
         }

         s = 0;
         for (i = 0; i < pdata.fe_matadd_nrows[box]; i++)
         {
            row = pdata.fe_matadd_rows[box][i];

            for (j = 0; j < pdata.fe_matadd_ncols[box]; j++)
            {
               col = pdata.fe_matadd_cols[box][j];

               for (k = 0; k < 3; k++)
               {
                  ilower[k] = pdata.fe_matadd_ilowers[box][k] +
                     data.fe_stencil_offsets[row][k];
                  iupper[k] = pdata.fe_matadd_iuppers[box][k] +
                     data.fe_stencil_offsets[row][k];
               }

               for (k = 0; k < size; k++)
               {
                  values[k] = pdata.fe_matadd_values[box][s];
               }
               s++;
               
               var = data.fe_stencil_vars[row];
               entry = data.fe_stencil_entries[row][col];
               if (entry > -1)
               {
                  HYPRE_SStructMatrixAddToBoxValues(A, part, ilower, iupper,
                                                    var, 1, &entry, values);
               }
            }
         }
      }
   }

   HYPRE_SStructMatrixAssemble(A);

   /*-----------------------------------------------------------
    * Set up the linear system
    *-----------------------------------------------------------*/

   HYPRE_SStructVectorCreate(MPI_COMM_WORLD, grid, &b);

   /* HYPRE_SSTRUCT is the default, so we don't have to call SetObjectType */
   if ( object_type != HYPRE_SSTRUCT )
   {
       HYPRE_SStructVectorSetObjectType(b, object_type);
   }

   HYPRE_SStructVectorInitialize(b);
   for (j = 0; j < data.max_boxsize; j++)
   {
      values[j] = 1.0;
   }
   for (part = 0; part < data.nparts; part++)
   {
      pdata = data.pdata[part];
      for (var = 0; var < pdata.nvars; var++)
      {
         for (box = 0; box < pdata.nboxes; box++)
         {
            GetVariableBox(pdata.ilowers[box], pdata.iuppers[box],
                           pdata.vartypes[var], ilower, iupper);
            HYPRE_SStructVectorSetBoxValues(b, part, ilower, iupper,
                                            var, values);
         }
      }
   }
   HYPRE_SStructVectorAssemble(b);

   HYPRE_SStructVectorCreate(MPI_COMM_WORLD, grid, &x);

   /* HYPRE_SSTRUCT is the default, so we don't have to call SetObjectType */
   if ( object_type != HYPRE_SSTRUCT )
   {
       HYPRE_SStructVectorSetObjectType(x, object_type);
   }

   HYPRE_SStructVectorInitialize(x);

   /*-----------------------------------------------------------
    * If requested, reset linear system so that it has
    * exact solution:
    *
    *   u(part,var,i,j,k) = (part+1)*(var+1)*cosine[(i+j+k)/10]
    * 
    *-----------------------------------------------------------*/

   if (cosine)
   {
      for (part = 0; part < data.nparts; part++)
      {
         pdata = data.pdata[part];
         for (var = 0; var < pdata.nvars; var++)
         {
            scale = (part+1.0)*(var+1.0);
            for (box = 0; box < pdata.nboxes; box++)
            {
               GetVariableBox(pdata.ilowers[box], pdata.iuppers[box], var,
                              ilower, iupper);
               SetCosineVector(scale, ilower, iupper, values);
               HYPRE_SStructVectorSetBoxValues(x, part, ilower, iupper,
                                               var, values);
            }
         }
      }
   }
   HYPRE_SStructVectorAssemble(x);

   hypre_EndTiming(time_index);
   hypre_PrintTiming("SStruct Interface", MPI_COMM_WORLD);
   hypre_FinalizeTiming(time_index);
   hypre_ClearTiming();

   /*-----------------------------------------------------------
    * Get the objects out
    * NOTE: This should go after the cosine part, but for the bug
    *-----------------------------------------------------------*/

   if (object_type == HYPRE_PARCSR)
   {
      HYPRE_SStructMatrixGetObject(A, (void **) &par_A);
      HYPRE_SStructVectorGetObject(b, (void **) &par_b);
      HYPRE_SStructVectorGetObject(x, (void **) &par_x);
   }
   else if (object_type == HYPRE_STRUCT)
   {
      HYPRE_SStructMatrixGetObject(A, (void **) &sA);
      HYPRE_SStructVectorGetObject(b, (void **) &sb);
      HYPRE_SStructVectorGetObject(x, (void **) &sx);
   }

   /*-----------------------------------------------------------
    * Finish resetting the linear system
    *-----------------------------------------------------------*/

   if (cosine)
   {
      /* This if/else is due to a bug in SStructMatvec */
      if (object_type == HYPRE_SSTRUCT)
      {
         /* Apply A to cosine vector to yield righthand side */
         hypre_SStructMatvec(1.0, A, x, 0.0, b);
         /* Reset initial guess to zero */
         hypre_SStructMatvec(0.0, A, b, 0.0, x);
      }
      else if (object_type == HYPRE_PARCSR)
      {
         /* Apply A to cosine vector to yield righthand side */
         HYPRE_ParCSRMatrixMatvec(1.0, par_A, par_x, 0.0, par_b );
         /* Reset initial guess to zero */
         HYPRE_ParCSRMatrixMatvec(0.0, par_A, par_b, 0.0, par_x );
      }
      else if (object_type == HYPRE_STRUCT)
      {
         /* Apply A to cosine vector to yield righthand side */
         hypre_StructMatvec(1.0, sA, sx, 0.0, sb);
         /* Reset initial guess to zero */
         hypre_StructMatvec(0.0, sA, sb, 0.0, sx);
      }
   }

   /*-----------------------------------------------------------
    * Print out the system and initial guess
    *-----------------------------------------------------------*/
   /*hypre_SStructMatvec(1.0, A, x, 2.0, b);
   HYPRE_ParCSRMatrixMatvec(1.0, par_A, par_x, 2.0, par_b );*/
                                                                                                               
   if (print_system)
   {
      HYPRE_SStructVectorGather(b);
      HYPRE_SStructVectorGather(x);
      HYPRE_SStructMatrixPrint("sstruct.out.A",  A, 0);
      HYPRE_SStructVectorPrint("sstruct.out.b",  b, 0);
      HYPRE_SStructVectorPrint("sstruct.out.x0", x, 0);
   }

   /*-----------------------------------------------------------
    * Debugging code
    *-----------------------------------------------------------*/

#if DEBUG
   {
      FILE *file;
      char  filename[255];
                       
      /* result is 1's on the interior of the grid */
      hypre_SStructMatvec(1.0, A, b, 0.0, x);
      HYPRE_SStructVectorPrint("sstruct.out.matvec", x, 0);

      /* result is all 1's */
      hypre_SStructCopy(b, x);
      HYPRE_SStructVectorPrint("sstruct.out.copy", x, 0);

      /* result is all 2's */
      hypre_SStructScale(2.0, x);
      HYPRE_SStructVectorPrint("sstruct.out.scale", x, 0);

      /* result is all 0's */
      hypre_SStructAxpy(-2.0, b, x);
      HYPRE_SStructVectorPrint("sstruct.out.axpy", x, 0);

      /* result is 1's with 0's on some boundaries */
      hypre_SStructCopy(b, x);
      sprintf(filename, "sstruct.out.gatherpre.%05d", myid);
      file = fopen(filename, "w");
      for (part = 0; part < data.nparts; part++)
      {
         pdata = data.pdata[part];
         for (var = 0; var < pdata.nvars; var++)
         {
            for (box = 0; box < pdata.nboxes; box++)
            {
               GetVariableBox(pdata.ilowers[box], pdata.iuppers[box], var,
                              ilower, iupper);
               HYPRE_SStructVectorGetBoxValues(x, part, ilower, iupper,
                                               var, values);
               fprintf(file, "\nPart %d, var %d, box %d:\n", part, var, box);
               for (i = 0; i < pdata.boxsizes[box]; i++)
               {
                  fprintf(file, "%e\n", values[i]);
               }
            }
         }
      }
      fclose(file);

      /* result is all 1's */
      HYPRE_SStructVectorGather(x);
      sprintf(filename, "sstruct.out.gatherpost.%05d", myid);
      file = fopen(filename, "w");
      for (part = 0; part < data.nparts; part++)
      {
         pdata = data.pdata[part];
         for (var = 0; var < pdata.nvars; var++)
         {
            for (box = 0; box < pdata.nboxes; box++)
            {
               GetVariableBox(pdata.ilowers[box], pdata.iuppers[box], var,
                              ilower, iupper);
               HYPRE_SStructVectorGetBoxValues(x, part, ilower, iupper,
                                               var, values);
               fprintf(file, "\nPart %d, var %d, box %d:\n", part, var, box);
               for (i = 0; i < pdata.boxsizes[box]; i++)
               {
                  fprintf(file, "%e\n", values[i]);
               }
            }
         }
      }

      /* re-initializes x to 0 */
      hypre_SStructAxpy(-1.0, b, x);
   }
#endif

   hypre_TFree(values);

   /*-----------------------------------------------------------
    * Solve the system using SysPFMG or Split
    *-----------------------------------------------------------*/

   if (solver_id == 3)
   {
      time_index = hypre_InitializeTiming("SysPFMG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_SStructSysPFMGCreate(MPI_COMM_WORLD, &solver);
      HYPRE_SStructSysPFMGSetMaxIter(solver, 100);
      HYPRE_SStructSysPFMGSetTol(solver, 1.0e-6);
      HYPRE_SStructSysPFMGSetRelChange(solver, 0);
      /* weighted Jacobi = 1; red-black GS = 2 */
      HYPRE_SStructSysPFMGSetRelaxType(solver, relax);
      if (usr_jacobi_weight)
      {
         HYPRE_SStructSysPFMGSetJacobiWeight(solver, jacobi_weight);
      }
      HYPRE_SStructSysPFMGSetNumPreRelax(solver, n_pre);
      HYPRE_SStructSysPFMGSetNumPostRelax(solver, n_post);
      HYPRE_SStructSysPFMGSetSkipRelax(solver, skip);
      /*HYPRE_StructPFMGSetDxyz(solver, dxyz);*/
      HYPRE_SStructSysPFMGSetPrintLevel(solver, 1);
      HYPRE_SStructSysPFMGSetLogging(solver, 1);
      HYPRE_SStructSysPFMGSetup(solver, A, b, x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("SysPFMG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_SStructSysPFMGSolve(solver, A, b, x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_SStructSysPFMGGetNumIterations(solver, &num_iterations);
      HYPRE_SStructSysPFMGGetFinalRelativeResidualNorm(solver, &final_res_norm);

      HYPRE_SStructSysPFMGDestroy(solver);
   }

   else if ((solver_id >= 0) && (solver_id < 10) && (solver_id != 3))
   {
      time_index = hypre_InitializeTiming("Split Setup");
      hypre_BeginTiming(time_index);

      HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &solver);
      HYPRE_SStructSplitSetMaxIter(solver, 100);
      HYPRE_SStructSplitSetTol(solver, 1.0e-6);
      if (solver_id == 0)
      {
         HYPRE_SStructSplitSetStructSolver(solver, HYPRE_SMG);
      }
      else if (solver_id == 1)
      {
         HYPRE_SStructSplitSetStructSolver(solver, HYPRE_PFMG);
      }
      else if (solver_id == 8)
      {
         HYPRE_SStructSplitSetStructSolver(solver, HYPRE_Jacobi);
      }
      HYPRE_SStructSplitSetup(solver, A, b, x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("Split Solve");
      hypre_BeginTiming(time_index);

      HYPRE_SStructSplitSolve(solver, A, b, x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_SStructSplitGetNumIterations(solver, &num_iterations);
      HYPRE_SStructSplitGetFinalRelativeResidualNorm(solver, &final_res_norm);

      HYPRE_SStructSplitDestroy(solver);
   }

   /*-----------------------------------------------------------
    * Solve the system using PCG
    *-----------------------------------------------------------*/

   if ((solver_id >= 10) && (solver_id < 20))
   {
      time_index = hypre_InitializeTiming("PCG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_SStructPCGCreate(MPI_COMM_WORLD, &solver);
      HYPRE_PCGSetMaxIter( (HYPRE_Solver) solver, 100 );
      HYPRE_PCGSetTol( (HYPRE_Solver) solver, 1.0e-06 );
      HYPRE_PCGSetTwoNorm( (HYPRE_Solver) solver, 1 );
      HYPRE_PCGSetRelChange( (HYPRE_Solver) solver, 0 );
      HYPRE_PCGSetPrintLevel( (HYPRE_Solver) solver, 1 );

      if ((solver_id == 10) || (solver_id == 11))
      {
         /* use Split solver as preconditioner */
         HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &precond);
         HYPRE_SStructSplitSetMaxIter(precond, 1);
         HYPRE_SStructSplitSetTol(precond, 0.0);
         HYPRE_SStructSplitSetZeroGuess(precond);
         if (solver_id == 10)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_SMG);
         }
         else if (solver_id == 11)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_PFMG);
         }
         HYPRE_PCGSetPrecond( (HYPRE_Solver) solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSolve,
                              (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSetup,
                              (HYPRE_Solver) precond);
      }

      else if (solver_id == 13)
      {
         /* use SysPFMG solver as preconditioner */
         HYPRE_SStructSysPFMGCreate(MPI_COMM_WORLD, &precond);
         HYPRE_SStructSysPFMGSetMaxIter(precond, 1);
         HYPRE_SStructSysPFMGSetTol(precond, 0.0);
         HYPRE_SStructSysPFMGSetZeroGuess(precond);
         /* weighted Jacobi = 1; red-black GS = 2 */
         HYPRE_SStructSysPFMGSetRelaxType(precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_SStructSysPFMGSetJacobiWeight(precond, jacobi_weight);
         }
         HYPRE_SStructSysPFMGSetNumPreRelax(precond, n_pre);
         HYPRE_SStructSysPFMGSetNumPostRelax(precond, n_post);
         HYPRE_SStructSysPFMGSetSkipRelax(precond, skip);
         /*HYPRE_StructPFMGSetDxyz(precond, dxyz);*/
         HYPRE_PCGSetPrecond( (HYPRE_Solver) solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_SStructSysPFMGSolve,
                              (HYPRE_PtrToSolverFcn) HYPRE_SStructSysPFMGSetup,
                              (HYPRE_Solver) precond);

      }
      else if (solver_id == 18)
      {
         /* use diagonal scaling as preconditioner */
         precond = NULL;
         HYPRE_PCGSetPrecond( (HYPRE_Solver) solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScale,
                              (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScaleSetup,
                              (HYPRE_Solver) precond);
      }

      HYPRE_PCGSetup( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                      (HYPRE_Vector) b, (HYPRE_Vector) x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("PCG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_PCGSolve( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                      (HYPRE_Vector) b, (HYPRE_Vector) x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_PCGGetNumIterations( (HYPRE_Solver) solver, &num_iterations );
      HYPRE_PCGGetFinalRelativeResidualNorm( (HYPRE_Solver) solver, &final_res_norm );
      HYPRE_SStructPCGDestroy(solver);

      if ((solver_id == 10) || (solver_id == 11))
      {
         HYPRE_SStructSplitDestroy(precond);
      }
      else if (solver_id == 13)
      {
         HYPRE_SStructSysPFMGDestroy(precond);
      }
   }

   /* begin lobpcg */

   /*-----------------------------------------------------------
    * Solve the eigenvalue problem using LOBPCG
    *-----------------------------------------------------------*/

   if ( lobpcgFlag && ( solver_id < 10 || solver_id >= 20 ) && verbosity )
     printf("\nLOBPCG works with solvers 10, 11, 13 and 18 only\n");

   if ( lobpcgFlag && (solver_id >= 10) && (solver_id < 20) ) {

     interpreter = hypre_CTAlloc(mv_InterfaceInterpreter,1);

     HYPRE_SStructSetupInterpreter( interpreter );
     HYPRE_SStructSetupMatvec(&matvec_fn);

     if (myid != 0)
       verbosity = 0;

     if ( pcgIterations > 0 ) {

       time_index = hypre_InitializeTiming("PCG Setup");
       hypre_BeginTiming(time_index);

       HYPRE_SStructPCGCreate(MPI_COMM_WORLD, &solver);
       HYPRE_PCGSetMaxIter( (HYPRE_Solver) solver, pcgIterations );
       HYPRE_PCGSetTol( (HYPRE_Solver) solver, pcgTol );
       HYPRE_PCGSetTwoNorm( (HYPRE_Solver) solver, 1 );
       HYPRE_PCGSetRelChange( (HYPRE_Solver) solver, 0 );
       HYPRE_PCGSetPrintLevel( (HYPRE_Solver) solver, 0 );
       
       if ((solver_id == 10) || (solver_id == 11))
	 {
	   /* use Split solver as preconditioner */
	   HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &precond);
	   HYPRE_SStructSplitSetMaxIter(precond, 1);
	   HYPRE_SStructSplitSetTol(precond, 0.0);
	   HYPRE_SStructSplitSetZeroGuess(precond);
	   if (solver_id == 10)
	     {
	       HYPRE_SStructSplitSetStructSolver(precond, HYPRE_SMG);
	     }
	   else if (solver_id == 11)
	     {
	       HYPRE_SStructSplitSetStructSolver(precond, HYPRE_PFMG);
	     }
	   HYPRE_PCGSetPrecond( (HYPRE_Solver) solver,
				(HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSolve,
				(HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSetup,
				(HYPRE_Solver) precond);
	 }
       
       else if (solver_id == 13)
	 {
	   /* use SysPFMG solver as preconditioner */
	   HYPRE_SStructSysPFMGCreate(MPI_COMM_WORLD, &precond);
	   HYPRE_SStructSysPFMGSetMaxIter(precond, 1);
	   HYPRE_SStructSysPFMGSetTol(precond, 0.0);
	   HYPRE_SStructSysPFMGSetZeroGuess(precond);
	   /* weighted Jacobi = 1; red-black GS = 2 */
	   HYPRE_SStructSysPFMGSetRelaxType(precond, 1);
	   HYPRE_SStructSysPFMGSetNumPreRelax(precond, n_pre);
	   HYPRE_SStructSysPFMGSetNumPostRelax(precond, n_post);
	   HYPRE_SStructSysPFMGSetSkipRelax(precond, skip);
	   /*HYPRE_StructPFMGSetDxyz(precond, dxyz);*/
	   HYPRE_PCGSetPrecond( (HYPRE_Solver) solver,
				(HYPRE_PtrToSolverFcn) HYPRE_SStructSysPFMGSolve,
				(HYPRE_PtrToSolverFcn) HYPRE_SStructSysPFMGSetup,
				(HYPRE_Solver) precond);
	   
	 }
       else if (solver_id == 18)
	 {
	   /* use diagonal scaling as preconditioner */
	   precond = NULL;
	   HYPRE_PCGSetPrecond( (HYPRE_Solver) solver,
				(HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScale,
				(HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScaleSetup,
				(HYPRE_Solver) precond);
	 }
       else if (solver_id != NO_SOLVER )
	 {
	   if ( verbosity )
	     printf("Solver ID not recognized - running inner PCG iterations without preconditioner\n\n");
	 }


       hypre_EndTiming(time_index);
       hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
       hypre_FinalizeTiming(time_index);
       hypre_ClearTiming();
       
       HYPRE_LOBPCGCreate(interpreter, &matvec_fn, (HYPRE_Solver*)&lobpcg_solver);
       HYPRE_LOBPCGSetMaxIter((HYPRE_Solver)lobpcg_solver, maxIterations);
       HYPRE_LOBPCGSetPrecondUsageMode((HYPRE_Solver)lobpcg_solver, pcgMode);
       HYPRE_LOBPCGSetTol((HYPRE_Solver)lobpcg_solver, tol);
       HYPRE_LOBPCGSetPrintLevel((HYPRE_Solver)lobpcg_solver, verbosity);
       
       HYPRE_LOBPCGSetPrecond((HYPRE_Solver)lobpcg_solver,
			      (HYPRE_PtrToSolverFcn) HYPRE_PCGSolve,
			      (HYPRE_PtrToSolverFcn) HYPRE_PCGSetup,
			      (HYPRE_Solver)solver);
       
       HYPRE_LOBPCGSetup((HYPRE_Solver)lobpcg_solver, (HYPRE_Matrix)A, 
			 (HYPRE_Vector)b, (HYPRE_Vector)x);
       
       eigenvectors = mv_MultiVectorCreateFromSampleVector( interpreter,
							       blockSize, 
							       x );
       eigenvalues = (double*) calloc( blockSize, sizeof(double) );
       
       if ( lobpcgSeed )
	 mv_MultiVectorSetRandom( eigenvectors, lobpcgSeed );
       else
	 mv_MultiVectorSetRandom( eigenvectors, (unsigned int)time(0) );
       
       time_index = hypre_InitializeTiming("PCG Solve");
       hypre_BeginTiming(time_index);
       
       HYPRE_LOBPCGSolve((HYPRE_Solver)lobpcg_solver, constrains, 
			 eigenvectors, eigenvalues );
       
       hypre_EndTiming(time_index);
       hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
       hypre_FinalizeTiming(time_index);
       hypre_ClearTiming();
       
       if ( checkOrtho ) {
	 
	 gramXX = utilities_FortranMatrixCreate();
	 identity = utilities_FortranMatrixCreate();
	 
	 utilities_FortranMatrixAllocateData( blockSize, blockSize, gramXX );
	 utilities_FortranMatrixAllocateData( blockSize, blockSize, identity );
	 
	 lobpcg_MultiVectorByMultiVector( eigenvectors, eigenvectors, gramXX );
	 utilities_FortranMatrixSetToIdentity( identity );
	 utilities_FortranMatrixAdd( -1, identity, gramXX, gramXX );
	 nonOrthF = utilities_FortranMatrixFNorm( gramXX );
	 if ( myid == 0 )
	   printf("Non-orthonormality of eigenvectors: %12.5e\n", nonOrthF);
	 
	 utilities_FortranMatrixDestroy( gramXX );
	 utilities_FortranMatrixDestroy( identity );
	 
       }
       
       if ( printLevel ) {
	 
	 if ( myid == 0 ) {	  
	   if ( (filePtr = fopen("values.txt", "w")) ) {
	     fprintf(filePtr, "%d\n", blockSize);
	     for ( i = 0; i < blockSize; i++ )
	       fprintf(filePtr, "%22.14e\n", eigenvalues[i]);
	     fclose(filePtr);
	   }
	   
	   if ( (filePtr = fopen("residuals.txt", "w")) ) {
	     residualNorms = HYPRE_LOBPCGResidualNorms( (HYPRE_Solver)lobpcg_solver );
	     residuals = utilities_FortranMatrixValues( residualNorms );
	     fprintf(filePtr, "%d\n", blockSize);
	     for ( i = 0; i < blockSize; i++ )
	       fprintf(filePtr, "%22.14e\n", residuals[i]);
	     fclose(filePtr);
	   }
	   
	   if ( printLevel > 1 ) {
	     
	     printBuffer = utilities_FortranMatrixCreate();
	     
	     iterations = HYPRE_LOBPCGIterations( (HYPRE_Solver)lobpcg_solver );
	     
	     eigenvaluesHistory = HYPRE_LOBPCGEigenvaluesHistory( (HYPRE_Solver)lobpcg_solver );
	     utilities_FortranMatrixSelectBlock( eigenvaluesHistory,
						 1, blockSize, 1, iterations + 1, printBuffer );
	     utilities_FortranMatrixPrint( printBuffer, "val_hist.txt" );
	     
	     residualNormsHistory = HYPRE_LOBPCGResidualNormsHistory( (HYPRE_Solver)lobpcg_solver );
	     utilities_FortranMatrixSelectBlock(residualNormsHistory, 
						1, blockSize, 1, iterations + 1, printBuffer );
	     utilities_FortranMatrixPrint( printBuffer, "res_hist.txt" );
	     
	     utilities_FortranMatrixDestroy( printBuffer );
	   }
	 }
       }
       
       HYPRE_SStructPCGDestroy(solver);
       
       if ((solver_id == 10) || (solver_id == 11))
	 {
	   HYPRE_SStructSplitDestroy(precond);
	 }
       else if (solver_id == 13)
	 {
	   HYPRE_SStructSysPFMGDestroy(precond);
	 }
       
       HYPRE_LOBPCGDestroy((HYPRE_Solver)lobpcg_solver);
       mv_MultiVectorDestroy( eigenvectors );
       free( eigenvalues );
       
     } 
     else {

       time_index = hypre_InitializeTiming("LOBPCG Setup");
       hypre_BeginTiming(time_index);
       
       HYPRE_LOBPCGCreate(interpreter, &matvec_fn, (HYPRE_Solver*)&solver);
       HYPRE_LOBPCGSetMaxIter( (HYPRE_Solver) solver, maxIterations );
       HYPRE_LOBPCGSetTol( (HYPRE_Solver) solver, tol );
       HYPRE_LOBPCGSetPrintLevel( (HYPRE_Solver) solver, verbosity );
       
       if ((solver_id == 10) || (solver_id == 11))
	 {
	   /* use Split solver as preconditioner */
	   HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &precond);
	   HYPRE_SStructSplitSetMaxIter(precond, 1);
	   HYPRE_SStructSplitSetTol(precond, 0.0);
	   HYPRE_SStructSplitSetZeroGuess(precond);
	   if (solver_id == 10)
	     {
	       HYPRE_SStructSplitSetStructSolver(precond, HYPRE_SMG);
	     }
	   else if (solver_id == 11)
	     {
	       HYPRE_SStructSplitSetStructSolver(precond, HYPRE_PFMG);
	     }
	   HYPRE_LOBPCGSetPrecond( (HYPRE_Solver) solver,
				   (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSolve,
				   (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSetup,
				   (HYPRE_Solver) precond);
	 }
       
       else if (solver_id == 13)
	 {
	   /* use SysPFMG solver as preconditioner */
	   HYPRE_SStructSysPFMGCreate(MPI_COMM_WORLD, &precond);
	   HYPRE_SStructSysPFMGSetMaxIter(precond, 1);
	   HYPRE_SStructSysPFMGSetTol(precond, 0.0);
	   HYPRE_SStructSysPFMGSetZeroGuess(precond);
	   /* weighted Jacobi = 1; red-black GS = 2 */
	   HYPRE_SStructSysPFMGSetRelaxType(precond, 1);
	   HYPRE_SStructSysPFMGSetNumPreRelax(precond, n_pre);
	   HYPRE_SStructSysPFMGSetNumPostRelax(precond, n_post);
	   HYPRE_SStructSysPFMGSetSkipRelax(precond, skip);
	   /*HYPRE_StructPFMGSetDxyz(precond, dxyz);*/
	   HYPRE_LOBPCGSetPrecond( (HYPRE_Solver) solver,
				   (HYPRE_PtrToSolverFcn) HYPRE_SStructSysPFMGSolve,
				   (HYPRE_PtrToSolverFcn) HYPRE_SStructSysPFMGSetup,
				   (HYPRE_Solver) precond);
	   
	 }
       else if (solver_id == 18)
	 {
	   /* use diagonal scaling as preconditioner */
	   precond = NULL;
	   HYPRE_LOBPCGSetPrecond( (HYPRE_Solver) solver,
				   (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScale,
				   (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScaleSetup,
				   (HYPRE_Solver) precond);
	 }
       else if (solver_id != NO_SOLVER )
	 {
	   if ( verbosity )
	     printf("Solver ID not recognized - running LOBPCG without preconditioner\n\n");
	 }
       
       HYPRE_LOBPCGSetup( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
			  (HYPRE_Vector) b, (HYPRE_Vector) x);
       
       hypre_EndTiming(time_index);
       hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
       hypre_FinalizeTiming(time_index);
       hypre_ClearTiming();
       
       eigenvectors = mv_MultiVectorCreateFromSampleVector( interpreter,
							       blockSize, 
							       x );
       eigenvalues = (double*) calloc( blockSize, sizeof(double) );
       
       if ( lobpcgSeed )
	 mv_MultiVectorSetRandom( eigenvectors, lobpcgSeed );
       else
	 mv_MultiVectorSetRandom( eigenvectors, (unsigned int)time(0) );
       
       time_index = hypre_InitializeTiming("LOBPCG Solve");
       hypre_BeginTiming(time_index);
       
       HYPRE_LOBPCGSolve
         ( (HYPRE_Solver) solver, constrains, eigenvectors, eigenvalues );
       
       hypre_EndTiming(time_index);
       hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
       hypre_FinalizeTiming(time_index);
       hypre_ClearTiming();
       
       if ( checkOrtho ) {
	 
	 gramXX = utilities_FortranMatrixCreate();
	 identity = utilities_FortranMatrixCreate();
	 
	 utilities_FortranMatrixAllocateData( blockSize, blockSize, gramXX );
	 utilities_FortranMatrixAllocateData( blockSize, blockSize, identity );
	 
	 lobpcg_MultiVectorByMultiVector( eigenvectors, eigenvectors, gramXX );
	 utilities_FortranMatrixSetToIdentity( identity );
	 utilities_FortranMatrixAdd( -1, identity, gramXX, gramXX );
	 nonOrthF = utilities_FortranMatrixFNorm( gramXX );
	 if ( myid == 0 )
	   printf("Non-orthonormality of eigenvectors: %12.5e\n", nonOrthF);
	 
	 utilities_FortranMatrixDestroy( gramXX );
	 utilities_FortranMatrixDestroy( identity );
	 
       }
       
       if ( printLevel ) {
	 
	 if ( myid == 0 ) {
	   if ( (filePtr = fopen("values.txt", "w")) ) {
	     fprintf(filePtr, "%d\n", blockSize);
	     for ( i = 0; i < blockSize; i++ )
	       fprintf(filePtr, "%22.14e\n", eigenvalues[i]);
	     fclose(filePtr);
	   }
	   
	   if ( (filePtr = fopen("residuals.txt", "w")) ) {
	     residualNorms = HYPRE_LOBPCGResidualNorms( (HYPRE_Solver)solver );
	     residuals = utilities_FortranMatrixValues( residualNorms );
	     fprintf(filePtr, "%d\n", blockSize);
	     for ( i = 0; i < blockSize; i++ )
	       fprintf(filePtr, "%22.14e\n", residuals[i]);
	     fclose(filePtr);
	   }
	   
	   if ( printLevel > 1 ) {
	     
	     printBuffer = utilities_FortranMatrixCreate();
	     
	     iterations = HYPRE_LOBPCGIterations( (HYPRE_Solver)solver );
	     
	     eigenvaluesHistory = HYPRE_LOBPCGEigenvaluesHistory( (HYPRE_Solver)solver );
	     utilities_FortranMatrixSelectBlock( eigenvaluesHistory,
						 1, blockSize, 1, iterations + 1, printBuffer );
	     utilities_FortranMatrixPrint( printBuffer, "val_hist.txt" );
	     
	     residualNormsHistory = HYPRE_LOBPCGResidualNormsHistory( (HYPRE_Solver)solver );
	     utilities_FortranMatrixSelectBlock(residualNormsHistory,
						1, blockSize, 1, iterations + 1, printBuffer );
	     utilities_FortranMatrixPrint( printBuffer, "res_hist.txt" );
	     
	     utilities_FortranMatrixDestroy( printBuffer );
	   }
	 }
       } 
       
       HYPRE_LOBPCGDestroy((HYPRE_Solver)solver);
       
       if ((solver_id == 10) || (solver_id == 11))
	 {
	   HYPRE_SStructSplitDestroy(precond);
	 }
       else if (solver_id == 13)
	 {
	   HYPRE_SStructSysPFMGDestroy(precond);
	 }
       
       mv_MultiVectorDestroy( eigenvectors );
       free( eigenvalues );
     }

     hypre_TFree( interpreter );

   }

   /* end lobpcg */

   /*-----------------------------------------------------------
    * Solve the system using ParCSR version of PCG
    *-----------------------------------------------------------*/

   if ((solver_id >= 20) && (solver_id < 30))
   {
      time_index = hypre_InitializeTiming("PCG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRPCGCreate(MPI_COMM_WORLD, &par_solver);
      HYPRE_PCGSetMaxIter( par_solver, 100 );
      HYPRE_PCGSetTol( par_solver, 1.0e-06 );
      HYPRE_PCGSetTwoNorm( par_solver, 1 );
      HYPRE_PCGSetRelChange( par_solver, 0 );
      HYPRE_PCGSetPrintLevel( par_solver, 1 );

      if (solver_id == 20)
      {
         /* use BoomerAMG as preconditioner */
         HYPRE_BoomerAMGCreate(&par_precond); 
         HYPRE_BoomerAMGSetCoarsenType(par_precond, 6);
         HYPRE_BoomerAMGSetStrongThreshold(par_precond, 0.25);
         HYPRE_BoomerAMGSetTol(par_precond, 0.0);
         HYPRE_BoomerAMGSetPrintLevel(par_precond, 1);
         HYPRE_BoomerAMGSetPrintFileName(par_precond, "sstruct.out.log");
         HYPRE_BoomerAMGSetMaxIter(par_precond, 1);
         HYPRE_PCGSetPrecond( par_solver,
                             (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                             (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup,
                             par_precond );
      }
      else if (solver_id == 22)
      {
         /* use ParaSails as preconditioner */
         HYPRE_ParCSRParaSailsCreate(MPI_COMM_WORLD, &par_precond ); 
	 HYPRE_ParCSRParaSailsSetParams(par_precond, 0.1, 1);
         HYPRE_PCGSetPrecond( par_solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_ParCSRParaSailsSolve,
                              (HYPRE_PtrToSolverFcn) HYPRE_ParCSRParaSailsSetup,
                              par_precond );
      }
      else if (solver_id == 23)
      {
         /* use Euclid as preconditioner */
         HYPRE_EuclidCreate(MPI_COMM_WORLD, &par_precond);
         HYPRE_EuclidSetParams(par_precond, argc, argv);
         HYPRE_PCGSetPrecond(par_solver,
                            (HYPRE_PtrToSolverFcn) HYPRE_EuclidSolve,
                            (HYPRE_PtrToSolverFcn) HYPRE_EuclidSetup,
                             par_precond);
      }

      else if (solver_id == 28)
      {
         /* use diagonal scaling as preconditioner */
         par_precond = NULL;
         HYPRE_PCGSetPrecond(  par_solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_ParCSRDiagScale,
                              (HYPRE_PtrToSolverFcn) HYPRE_ParCSRDiagScaleSetup,
                               par_precond );
      }

      HYPRE_PCGSetup( par_solver, (HYPRE_Matrix) par_A,
                      (HYPRE_Vector) par_b, (HYPRE_Vector) par_x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("PCG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_PCGSolve( par_solver, (HYPRE_Matrix) par_A,
                      (HYPRE_Vector) par_b, (HYPRE_Vector) par_x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_PCGGetNumIterations( par_solver, &num_iterations );
      HYPRE_PCGGetFinalRelativeResidualNorm( par_solver, &final_res_norm );
      HYPRE_ParCSRPCGDestroy(par_solver);

      if (solver_id == 20)
      {
         HYPRE_BoomerAMGDestroy(par_precond);
      }
      else if (solver_id == 22)
      {
         HYPRE_ParCSRParaSailsDestroy(par_precond);
      }
      else if (solver_id == 23)
      {
         HYPRE_EuclidDestroy(par_precond);
      }

   }

   /*-----------------------------------------------------------
    * Solve the system using GMRES
    *-----------------------------------------------------------*/

   if ((solver_id >= 30) && (solver_id < 40))
   {
      time_index = hypre_InitializeTiming("GMRES Setup");
      hypre_BeginTiming(time_index);

      HYPRE_SStructGMRESCreate(MPI_COMM_WORLD, &solver);
      HYPRE_GMRESSetKDim( (HYPRE_Solver) solver, 5 );
      HYPRE_GMRESSetMaxIter( (HYPRE_Solver) solver, 100 );
      HYPRE_GMRESSetTol( (HYPRE_Solver) solver, 1.0e-06 );
      HYPRE_GMRESSetPrintLevel( (HYPRE_Solver) solver, 1 );
      HYPRE_GMRESSetLogging( (HYPRE_Solver) solver, 1 );

      if ((solver_id == 30) || (solver_id == 31))
      {
         /* use Split solver as preconditioner */
         HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &precond);
         HYPRE_SStructSplitSetMaxIter(precond, 1);
         HYPRE_SStructSplitSetTol(precond, 0.0);
         HYPRE_SStructSplitSetZeroGuess(precond);
         if (solver_id == 30)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_SMG);
         }
         else if (solver_id == 31)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_PFMG);
         }
         HYPRE_GMRESSetPrecond( (HYPRE_Solver) solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSetup,
                                (HYPRE_Solver) precond );
      }

      else if (solver_id == 38)
      {
         /* use diagonal scaling as preconditioner */
         precond = NULL;
         HYPRE_GMRESSetPrecond( (HYPRE_Solver) solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScale,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScaleSetup,
                                (HYPRE_Solver) precond );
      }

      HYPRE_GMRESSetup( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                        (HYPRE_Vector) b, (HYPRE_Vector) x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("GMRES Solve");
      hypre_BeginTiming(time_index);

      HYPRE_GMRESSolve( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                        (HYPRE_Vector) b, (HYPRE_Vector) x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_GMRESGetNumIterations( (HYPRE_Solver) solver, &num_iterations );
      HYPRE_GMRESGetFinalRelativeResidualNorm( (HYPRE_Solver) solver, &final_res_norm );
      HYPRE_SStructGMRESDestroy(solver);

      if ((solver_id == 30) || (solver_id == 31))
      {
         HYPRE_SStructSplitDestroy(precond);
      }
   }

   /*-----------------------------------------------------------
    * Solve the system using ParCSR version of GMRES
    *-----------------------------------------------------------*/

   if ((solver_id >= 40) && (solver_id < 50))
   {
      time_index = hypre_InitializeTiming("GMRES Setup");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRGMRESCreate(MPI_COMM_WORLD, &par_solver);
      HYPRE_GMRESSetKDim(par_solver, 5);
      HYPRE_GMRESSetMaxIter(par_solver, 100);
      HYPRE_GMRESSetTol(par_solver, 1.0e-06);
      HYPRE_GMRESSetPrintLevel(par_solver, 1);
      HYPRE_GMRESSetLogging(par_solver, 1);

      if (solver_id == 40)
      {
         /* use BoomerAMG as preconditioner */
         HYPRE_BoomerAMGCreate(&par_precond); 
         HYPRE_BoomerAMGSetCoarsenType(par_precond, 6);
         HYPRE_BoomerAMGSetStrongThreshold(par_precond, 0.25);
         HYPRE_BoomerAMGSetTol(par_precond, 0.0);
         HYPRE_BoomerAMGSetPrintLevel(par_precond, 1);
         HYPRE_BoomerAMGSetPrintFileName(par_precond, "sstruct.out.log");
         HYPRE_BoomerAMGSetMaxIter(par_precond, 1);
         HYPRE_GMRESSetPrecond( par_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup,
                                par_precond);
      }
      else if (solver_id == 41)
      {
         /* use Euclid as preconditioner */
         HYPRE_EuclidCreate(MPI_COMM_WORLD, &par_precond);
         HYPRE_EuclidSetParams(par_precond, argc, argv);
         HYPRE_GMRESSetPrecond(par_solver,
                            (HYPRE_PtrToSolverFcn) HYPRE_EuclidSolve,
                            (HYPRE_PtrToSolverFcn) HYPRE_EuclidSetup,
                             par_precond);
      }

      else if (solver_id == 42)
      {
         /* use ParaSails as preconditioner */
         HYPRE_ParCSRParaSailsCreate(MPI_COMM_WORLD, &par_precond ); 
	 HYPRE_ParCSRParaSailsSetParams(par_precond, 0.1, 1);
	 HYPRE_ParCSRParaSailsSetSym(par_precond, 0);
         HYPRE_GMRESSetPrecond( par_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_ParCSRParaSailsSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_ParCSRParaSailsSetup,
                                par_precond);
      }

      HYPRE_GMRESSetup( par_solver, (HYPRE_Matrix) par_A,
                        (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("GMRES Solve");
      hypre_BeginTiming(time_index);

      HYPRE_GMRESSolve( par_solver, (HYPRE_Matrix) par_A,
                        (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_GMRESGetNumIterations( par_solver, &num_iterations);
      HYPRE_GMRESGetFinalRelativeResidualNorm( par_solver, &final_res_norm);
      HYPRE_ParCSRGMRESDestroy(par_solver);

      if (solver_id == 40)
      {
         HYPRE_BoomerAMGDestroy(par_precond);
      }
      else if (solver_id == 41)
      {
         HYPRE_EuclidDestroy(par_precond);
      }
      else if (solver_id == 42)
      {
         HYPRE_ParCSRParaSailsDestroy(par_precond);
      }
   }

   /*-----------------------------------------------------------
    * Solve the system using BiCGSTAB
    *-----------------------------------------------------------*/

   if ((solver_id >= 50) && (solver_id < 60))
   {
      time_index = hypre_InitializeTiming("BiCGSTAB Setup");
      hypre_BeginTiming(time_index);

      HYPRE_SStructBiCGSTABCreate(MPI_COMM_WORLD, &solver);
      HYPRE_BiCGSTABSetMaxIter( (HYPRE_Solver) solver, 100 );
      HYPRE_BiCGSTABSetTol( (HYPRE_Solver) solver, 1.0e-06 );
      HYPRE_BiCGSTABSetPrintLevel( (HYPRE_Solver) solver, 1 );
      HYPRE_BiCGSTABSetLogging( (HYPRE_Solver) solver, 1 );

      if ((solver_id == 50) || (solver_id == 51))
      {
         /* use Split solver as preconditioner */
         HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &precond);
         HYPRE_SStructSplitSetMaxIter(precond, 1);
         HYPRE_SStructSplitSetTol(precond, 0.0);
         HYPRE_SStructSplitSetZeroGuess(precond);
         if (solver_id == 50)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_SMG);
         }
         else if (solver_id == 51)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_PFMG);
         }
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver) solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSetup,
                                (HYPRE_Solver) precond );
      }

      else if (solver_id == 58)
      {
         /* use diagonal scaling as preconditioner */
         precond = NULL;
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver) solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScale,
                                (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScaleSetup,
                                (HYPRE_Solver) precond );
      }

      HYPRE_BiCGSTABSetup( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                        (HYPRE_Vector) b, (HYPRE_Vector) x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("BiCGSTAB Solve");
      hypre_BeginTiming(time_index);

      HYPRE_BiCGSTABSolve( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                        (HYPRE_Vector) b, (HYPRE_Vector) x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_BiCGSTABGetNumIterations( (HYPRE_Solver) solver, &num_iterations );
      HYPRE_BiCGSTABGetFinalRelativeResidualNorm( (HYPRE_Solver) solver, &final_res_norm );
      HYPRE_SStructBiCGSTABDestroy(solver);

      if ((solver_id == 50) || (solver_id == 51))
      {
         HYPRE_SStructSplitDestroy(precond);
      }
   }

   /*-----------------------------------------------------------
    * Solve the system using ParCSR version of BiCGSTAB
    *-----------------------------------------------------------*/

   if ((solver_id >= 60) && (solver_id < 70))
   {
      time_index = hypre_InitializeTiming("BiCGSTAB Setup");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRBiCGSTABCreate(MPI_COMM_WORLD, &par_solver);
      HYPRE_BiCGSTABSetMaxIter(par_solver, 100);
      HYPRE_BiCGSTABSetTol(par_solver, 1.0e-06);
      HYPRE_BiCGSTABSetPrintLevel(par_solver, 1);
      HYPRE_BiCGSTABSetLogging(par_solver, 1);

      if (solver_id == 60)
      {
         /* use BoomerAMG as preconditioner */
         HYPRE_BoomerAMGCreate(&par_precond); 
         HYPRE_BoomerAMGSetCoarsenType(par_precond, 6);
         HYPRE_BoomerAMGSetStrongThreshold(par_precond, 0.25);
         HYPRE_BoomerAMGSetTol(par_precond, 0.0);
         HYPRE_BoomerAMGSetPrintLevel(par_precond, 1);
         HYPRE_BoomerAMGSetPrintFileName(par_precond, "sstruct.out.log");
         HYPRE_BoomerAMGSetMaxIter(par_precond, 1);
         HYPRE_BiCGSTABSetPrecond( par_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup,
                                par_precond);
      }
      else if (solver_id == 61)
      {
         /* use Euclid as preconditioner */
         HYPRE_EuclidCreate(MPI_COMM_WORLD, &par_precond);
         HYPRE_EuclidSetParams(par_precond, argc, argv);
         HYPRE_BiCGSTABSetPrecond(par_solver,
                                 (HYPRE_PtrToSolverFcn) HYPRE_EuclidSolve,
                                 (HYPRE_PtrToSolverFcn) HYPRE_EuclidSetup,
                                  par_precond);
      }

      else if (solver_id == 62)
      {
         /* use ParaSails as preconditioner */
         HYPRE_ParCSRParaSailsCreate(MPI_COMM_WORLD, &par_precond ); 
	 HYPRE_ParCSRParaSailsSetParams(par_precond, 0.1, 1);
	 HYPRE_ParCSRParaSailsSetSym(par_precond, 0);
         HYPRE_BiCGSTABSetPrecond( par_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_ParCSRParaSailsSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_ParCSRParaSailsSetup,
                                par_precond);
      }

      HYPRE_BiCGSTABSetup( par_solver, (HYPRE_Matrix) par_A,
                        (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("BiCGSTAB Solve");
      hypre_BeginTiming(time_index);

      HYPRE_BiCGSTABSolve( par_solver, (HYPRE_Matrix) par_A,
                        (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_BiCGSTABGetNumIterations( par_solver, &num_iterations);
      HYPRE_BiCGSTABGetFinalRelativeResidualNorm( par_solver, &final_res_norm);
      HYPRE_ParCSRBiCGSTABDestroy(par_solver);

      if (solver_id == 60)
      {
         HYPRE_BoomerAMGDestroy(par_precond);
      }
      else if (solver_id == 61)
      {
         HYPRE_EuclidDestroy(par_precond);
      }
      else if (solver_id == 62)
      {
         HYPRE_ParCSRParaSailsDestroy(par_precond);
      }
   }



 /*-----------------------------------------------------------
    * Solve the system using Flexible GMRES
    *-----------------------------------------------------------*/

   if ((solver_id >= 70) && (solver_id < 80))
   {
      time_index = hypre_InitializeTiming("FlexGMRES Setup");
      hypre_BeginTiming(time_index);

      HYPRE_SStructFlexGMRESCreate(MPI_COMM_WORLD, &solver);
      HYPRE_FlexGMRESSetKDim( (HYPRE_Solver) solver, 5 );
      HYPRE_FlexGMRESSetMaxIter( (HYPRE_Solver) solver, 100 );
      HYPRE_FlexGMRESSetTol( (HYPRE_Solver) solver, 1.0e-06 );
      HYPRE_FlexGMRESSetPrintLevel( (HYPRE_Solver) solver, 1 );
      HYPRE_FlexGMRESSetLogging( (HYPRE_Solver) solver, 1 );

      if ((solver_id == 70) || (solver_id == 71))
      {
         /* use Split solver as preconditioner */
         HYPRE_SStructSplitCreate(MPI_COMM_WORLD, &precond);
         HYPRE_SStructSplitSetMaxIter(precond, 1);
         HYPRE_SStructSplitSetTol(precond, 0.0);
         HYPRE_SStructSplitSetZeroGuess(precond);
         if (solver_id == 70)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_SMG);
         }
         else if (solver_id == 71)
         {
            HYPRE_SStructSplitSetStructSolver(precond, HYPRE_PFMG);
         }
         HYPRE_FlexGMRESSetPrecond( (HYPRE_Solver) solver,
                                    (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSolve,
                                    (HYPRE_PtrToSolverFcn) HYPRE_SStructSplitSetup,
                                    (HYPRE_Solver) precond );
      }

      else if (solver_id == 78)
      {
         /* use diagonal scaling as preconditioner */
         precond = NULL;
         HYPRE_FlexGMRESSetPrecond( (HYPRE_Solver) solver,
                                    (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScale,
                                    (HYPRE_PtrToSolverFcn) HYPRE_SStructDiagScaleSetup,
                                    (HYPRE_Solver) precond );
      }

      HYPRE_FlexGMRESSetup( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                            (HYPRE_Vector) b, (HYPRE_Vector) x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("FlexGMRES Solve");
      hypre_BeginTiming(time_index);

      HYPRE_FlexGMRESSolve( (HYPRE_Solver) solver, (HYPRE_Matrix) A,
                        (HYPRE_Vector) b, (HYPRE_Vector) x );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_FlexGMRESGetNumIterations( (HYPRE_Solver) solver, &num_iterations );
      HYPRE_FlexGMRESGetFinalRelativeResidualNorm( (HYPRE_Solver) solver, &final_res_norm );
      HYPRE_SStructFlexGMRESDestroy(solver);

      if ((solver_id == 70) || (solver_id == 71))
      {
         HYPRE_SStructSplitDestroy(precond);
      }
   }

   /*-----------------------------------------------------------
    * Solve the system using ParCSR version of Flexible GMRES
    *-----------------------------------------------------------*/

   if ((solver_id >= 80) && (solver_id < 90))
   {
      time_index = hypre_InitializeTiming("FlexGMRES Setup");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRFlexGMRESCreate(MPI_COMM_WORLD, &par_solver);
      HYPRE_FlexGMRESSetKDim(par_solver, 5);
      HYPRE_FlexGMRESSetMaxIter(par_solver, 100);
      HYPRE_FlexGMRESSetTol(par_solver, 1.0e-06);
      HYPRE_FlexGMRESSetPrintLevel(par_solver, 1);
      HYPRE_FlexGMRESSetLogging(par_solver, 1);

      if (solver_id == 80)
      {
         /* use BoomerAMG as preconditioner */
         HYPRE_BoomerAMGCreate(&par_precond); 
         HYPRE_BoomerAMGSetCoarsenType(par_precond, 6);
         HYPRE_BoomerAMGSetStrongThreshold(par_precond, 0.25);
         HYPRE_BoomerAMGSetTol(par_precond, 0.0);
         HYPRE_BoomerAMGSetPrintLevel(par_precond, 1);
         HYPRE_BoomerAMGSetPrintFileName(par_precond, "sstruct.out.log");
         HYPRE_BoomerAMGSetMaxIter(par_precond, 1);
         HYPRE_FlexGMRESSetPrecond( par_solver,
                                    (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                                    (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup,
                                    par_precond);
      }

      HYPRE_FlexGMRESSetup( par_solver, (HYPRE_Matrix) par_A,
                            (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("FlexGMRES Solve");
      hypre_BeginTiming(time_index);

      HYPRE_FlexGMRESSolve( par_solver, (HYPRE_Matrix) par_A,
                        (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_FlexGMRESGetNumIterations( par_solver, &num_iterations);
      HYPRE_FlexGMRESGetFinalRelativeResidualNorm( par_solver, &final_res_norm);
      HYPRE_ParCSRFlexGMRESDestroy(par_solver);

      if (solver_id == 80)
      {
         HYPRE_BoomerAMGDestroy(par_precond);
      }
   }


  /*-----------------------------------------------------------
    * Solve the system using ParCSR version of LGMRES
    *-----------------------------------------------------------*/

   if ((solver_id >= 90) && (solver_id < 100))
   {
      time_index = hypre_InitializeTiming("LGMRES Setup");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRLGMRESCreate(MPI_COMM_WORLD, &par_solver);
      HYPRE_LGMRESSetKDim(par_solver, 10);
      HYPRE_LGMRESSetAugDim(par_solver, 2);
      HYPRE_LGMRESSetMaxIter(par_solver, 100);
      HYPRE_LGMRESSetTol(par_solver, 1.0e-06);
      HYPRE_LGMRESSetPrintLevel(par_solver, 1);
      HYPRE_LGMRESSetLogging(par_solver, 1);

      if (solver_id == 90)
      {
         /* use BoomerAMG as preconditioner */
         HYPRE_BoomerAMGCreate(&par_precond); 
         HYPRE_BoomerAMGSetCoarsenType(par_precond, 6);
         HYPRE_BoomerAMGSetStrongThreshold(par_precond, 0.25);
         HYPRE_BoomerAMGSetTol(par_precond, 0.0);
         HYPRE_BoomerAMGSetPrintLevel(par_precond, 1);
         HYPRE_BoomerAMGSetPrintFileName(par_precond, "sstruct.out.log");
         HYPRE_BoomerAMGSetMaxIter(par_precond, 1);
         HYPRE_LGMRESSetPrecond( par_solver,
                                    (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSolve,
                                    (HYPRE_PtrToSolverFcn) HYPRE_BoomerAMGSetup,
                                    par_precond);
      }

      HYPRE_LGMRESSetup( par_solver, (HYPRE_Matrix) par_A,
                            (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("LGMRES Solve");
      hypre_BeginTiming(time_index);

      HYPRE_LGMRESSolve( par_solver, (HYPRE_Matrix) par_A,
                        (HYPRE_Vector) par_b, (HYPRE_Vector) par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_LGMRESGetNumIterations( par_solver, &num_iterations);
      HYPRE_LGMRESGetFinalRelativeResidualNorm( par_solver, &final_res_norm);
      HYPRE_ParCSRLGMRESDestroy(par_solver);

      if (solver_id == 90)
      {
         HYPRE_BoomerAMGDestroy(par_precond);
      }
   }





   /*-----------------------------------------------------------
    * Solve the system using ParCSR hybrid DSCG/BoomerAMG
    *-----------------------------------------------------------*/

   if (solver_id == 120) 
   {
      time_index = hypre_InitializeTiming("Hybrid Setup");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRHybridCreate(&par_solver);
      HYPRE_ParCSRHybridSetTol(par_solver, 1.0e-06);
      HYPRE_ParCSRHybridSetTwoNorm(par_solver, 1);
      HYPRE_ParCSRHybridSetRelChange(par_solver, 0);
      HYPRE_ParCSRHybridSetPrintLevel(par_solver,1);
      HYPRE_ParCSRHybridSetLogging(par_solver,1);
      HYPRE_ParCSRHybridSetup(par_solver,par_A,par_b,par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      time_index = hypre_InitializeTiming("Hybrid Solve");
      hypre_BeginTiming(time_index);

      HYPRE_ParCSRHybridSolve(par_solver,par_A,par_b,par_x);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index); 
      hypre_ClearTiming();

      HYPRE_ParCSRHybridGetNumIterations(par_solver, &num_iterations);
      HYPRE_ParCSRHybridGetFinalRelativeResidualNorm(par_solver, &final_res_norm);

      HYPRE_ParCSRHybridDestroy(par_solver);
   }

   /*-----------------------------------------------------------
    * Solve the system using Struct solvers
    *-----------------------------------------------------------*/

   if (solver_id == 200)
   {
      time_index = hypre_InitializeTiming("SMG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructSMGCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_StructSMGSetMemoryUse(struct_solver, 0);
      HYPRE_StructSMGSetMaxIter(struct_solver, 50);
      HYPRE_StructSMGSetTol(struct_solver, 1.0e-6);
      HYPRE_StructSMGSetRelChange(struct_solver, 0);
      HYPRE_StructSMGSetNumPreRelax(struct_solver, n_pre);
      HYPRE_StructSMGSetNumPostRelax(struct_solver, n_post);
      HYPRE_StructSMGSetPrintLevel(struct_solver, 1);
      HYPRE_StructSMGSetLogging(struct_solver, 1);
      HYPRE_StructSMGSetup(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("SMG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_StructSMGSolve(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_StructSMGGetNumIterations(struct_solver, &num_iterations);
      HYPRE_StructSMGGetFinalRelativeResidualNorm(struct_solver, &final_res_norm);
      HYPRE_StructSMGDestroy(struct_solver);
   }

   else if ( solver_id == 201 || solver_id == 203 || solver_id == 204 )
   {
      time_index = hypre_InitializeTiming("PFMG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructPFMGCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_StructPFMGSetMaxIter(struct_solver, 50);
      HYPRE_StructPFMGSetTol(struct_solver, 1.0e-06);
      HYPRE_StructPFMGSetRelChange(struct_solver, 0);
      HYPRE_StructPFMGSetRAPType(struct_solver, rap);
      HYPRE_StructPFMGSetRelaxType(struct_solver, relax);
      if (usr_jacobi_weight)
      {
         HYPRE_StructPFMGSetJacobiWeight(struct_solver, jacobi_weight);
      }
      HYPRE_StructPFMGSetNumPreRelax(struct_solver, n_pre);
      HYPRE_StructPFMGSetNumPostRelax(struct_solver, n_post);
      HYPRE_StructPFMGSetSkipRelax(struct_solver, skip);
      /*HYPRE_StructPFMGSetDxyz(struct_solver, dxyz);*/
      HYPRE_StructPFMGSetPrintLevel(struct_solver, 1);
      HYPRE_StructPFMGSetLogging(struct_solver, 1);
      HYPRE_StructPFMGSetup(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("PFMG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_StructPFMGSolve(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_StructPFMGGetNumIterations(struct_solver, &num_iterations);
      HYPRE_StructPFMGGetFinalRelativeResidualNorm(struct_solver, &final_res_norm);
      HYPRE_StructPFMGDestroy(struct_solver);
   }

   /*-----------------------------------------------------------
    * Solve the system using SparseMSG
    *-----------------------------------------------------------*/

   else if (solver_id == 202)
   {
      time_index = hypre_InitializeTiming("SparseMSG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructSparseMSGCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_StructSparseMSGSetMaxIter(struct_solver, 50);
      HYPRE_StructSparseMSGSetJump(struct_solver, jump);
      HYPRE_StructSparseMSGSetTol(struct_solver, 1.0e-06);
      HYPRE_StructSparseMSGSetRelChange(struct_solver, 0);
      HYPRE_StructSparseMSGSetRelaxType(struct_solver, relax);
      if (usr_jacobi_weight)
      {
         HYPRE_StructSparseMSGSetJacobiWeight(struct_solver, jacobi_weight);
      }
      HYPRE_StructSparseMSGSetNumPreRelax(struct_solver, n_pre);
      HYPRE_StructSparseMSGSetNumPostRelax(struct_solver, n_post);
      HYPRE_StructSparseMSGSetPrintLevel(struct_solver, 1);
      HYPRE_StructSparseMSGSetLogging(struct_solver, 1);
      HYPRE_StructSparseMSGSetup(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("SparseMSG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_StructSparseMSGSolve(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_StructSparseMSGGetNumIterations(struct_solver, &num_iterations);
      HYPRE_StructSparseMSGGetFinalRelativeResidualNorm(struct_solver,
                                                        &final_res_norm);
      HYPRE_StructSparseMSGDestroy(struct_solver);
   }

   /*-----------------------------------------------------------
    * Solve the system using Jacobi
    *-----------------------------------------------------------*/

   else if ( solver_id == 208 )
   {
      time_index = hypre_InitializeTiming("Jacobi Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructJacobiCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_StructJacobiSetMaxIter(struct_solver, 100);
      HYPRE_StructJacobiSetTol(struct_solver, 1.0e-06);
      HYPRE_StructJacobiSetup(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("Jacobi Solve");
      hypre_BeginTiming(time_index);

      HYPRE_StructJacobiSolve(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();
   
      HYPRE_StructJacobiGetNumIterations(struct_solver, &num_iterations);
      HYPRE_StructJacobiGetFinalRelativeResidualNorm(struct_solver,
                                                     &final_res_norm);
      HYPRE_StructJacobiDestroy(struct_solver);
   }

   /*-----------------------------------------------------------
    * Solve the system using CG
    *-----------------------------------------------------------*/

   if ((solver_id > 209) && (solver_id < 220))
   {
      time_index = hypre_InitializeTiming("PCG Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructPCGCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_PCGSetMaxIter( (HYPRE_Solver)struct_solver, 100 );
      HYPRE_PCGSetTol( (HYPRE_Solver)struct_solver, 1.0e-06 );
      HYPRE_PCGSetTwoNorm( (HYPRE_Solver)struct_solver, 1 );
      HYPRE_PCGSetRelChange( (HYPRE_Solver)struct_solver, 0 );
      HYPRE_PCGSetPrintLevel( (HYPRE_Solver)struct_solver, 1 );

      if (solver_id == 210)
      {
         /* use symmetric SMG as preconditioner */
         HYPRE_StructSMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSMGSetMemoryUse(struct_precond, 0);
         HYPRE_StructSMGSetMaxIter(struct_precond, 1);
         HYPRE_StructSMGSetTol(struct_precond, 0.0);
         HYPRE_StructSMGSetZeroGuess(struct_precond);
         HYPRE_StructSMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSMGSetLogging(struct_precond, 0);
         HYPRE_PCGSetPrecond( (HYPRE_Solver) struct_solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructSMGSolve,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructSMGSetup,
                              (HYPRE_Solver) struct_precond);
      }

      else if (solver_id == 211)
      {
         /* use symmetric PFMG as preconditioner */
         HYPRE_StructPFMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructPFMGSetMaxIter(struct_precond, 1);
         HYPRE_StructPFMGSetTol(struct_precond, 0.0);
         HYPRE_StructPFMGSetZeroGuess(struct_precond);
         HYPRE_StructPFMGSetRAPType(struct_precond, rap);
         HYPRE_StructPFMGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructPFMGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructPFMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructPFMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructPFMGSetSkipRelax(struct_precond, skip);
         /*HYPRE_StructPFMGSetDxyz(struct_precond, dxyz);*/
         HYPRE_StructPFMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructPFMGSetLogging(struct_precond, 0);
         HYPRE_PCGSetPrecond( (HYPRE_Solver) struct_solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructPFMGSolve,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructPFMGSetup,
                              (HYPRE_Solver) struct_precond);
      }

      else if (solver_id == 212)
      {
         /* use symmetric SparseMSG as preconditioner */
         HYPRE_StructSparseMSGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSparseMSGSetMaxIter(struct_precond, 1);
         HYPRE_StructSparseMSGSetJump(struct_precond, jump);
         HYPRE_StructSparseMSGSetTol(struct_precond, 0.0);
         HYPRE_StructSparseMSGSetZeroGuess(struct_precond);
         HYPRE_StructSparseMSGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructSparseMSGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructSparseMSGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSparseMSGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSparseMSGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSparseMSGSetLogging(struct_precond, 0);
         HYPRE_PCGSetPrecond( (HYPRE_Solver) struct_solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructSparseMSGSolve,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructSparseMSGSetup,
                              (HYPRE_Solver) struct_precond);
      }

      else if (solver_id == 217)
      {
         /* use two-step Jacobi as preconditioner */
         HYPRE_StructJacobiCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructJacobiSetMaxIter(struct_precond, 2);
         HYPRE_StructJacobiSetTol(struct_precond, 0.0);
         HYPRE_StructJacobiSetZeroGuess(struct_precond);
         HYPRE_PCGSetPrecond( (HYPRE_Solver) struct_solver,
                               (HYPRE_PtrToSolverFcn) HYPRE_StructJacobiSolve,
                               (HYPRE_PtrToSolverFcn) HYPRE_StructJacobiSetup,
                               (HYPRE_Solver) struct_precond);
      }

      else if (solver_id == 218)
      {
         /* use diagonal scaling as preconditioner */
#ifdef HYPRE_USE_PTHREADS
         for (i = 0; i < hypre_NumThreads; i++)
         {
            struct_precond[i] = NULL;
         }
#else
         struct_precond = NULL;
#endif
         HYPRE_PCGSetPrecond( (HYPRE_Solver) struct_solver,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructDiagScale,
                              (HYPRE_PtrToSolverFcn) HYPRE_StructDiagScaleSetup,
                              (HYPRE_Solver) struct_precond);
      }

      HYPRE_PCGSetup
         ( (HYPRE_Solver)struct_solver, (HYPRE_Matrix)sA, (HYPRE_Vector)sb, 
                                                          (HYPRE_Vector)sx );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("PCG Solve");
      hypre_BeginTiming(time_index);

      HYPRE_PCGSolve
         ( (HYPRE_Solver) struct_solver, (HYPRE_Matrix)sA, (HYPRE_Vector)sb, 
                                                           (HYPRE_Vector)sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_PCGGetNumIterations( (HYPRE_Solver)struct_solver, &num_iterations );
      HYPRE_PCGGetFinalRelativeResidualNorm( (HYPRE_Solver)struct_solver, &final_res_norm );
      HYPRE_StructPCGDestroy(struct_solver);

      if (solver_id == 210)
      {
         HYPRE_StructSMGDestroy(struct_precond);
      }
      else if (solver_id == 211)
      {
         HYPRE_StructPFMGDestroy(struct_precond);
      }
      else if (solver_id == 212)
      {
         HYPRE_StructSparseMSGDestroy(struct_precond);
      }
      else if (solver_id == 217)
      {
         HYPRE_StructJacobiDestroy(struct_precond);
      }
   }

   /*-----------------------------------------------------------
    * Solve the system using Hybrid
    *-----------------------------------------------------------*/

   if ((solver_id > 219) && (solver_id < 230))
   {
      time_index = hypre_InitializeTiming("Hybrid Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructHybridCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_StructHybridSetDSCGMaxIter(struct_solver, 100);
      HYPRE_StructHybridSetPCGMaxIter(struct_solver, 100);
      HYPRE_StructHybridSetTol(struct_solver, 1.0e-06);
      /*HYPRE_StructHybridSetPCGAbsoluteTolFactor(struct_solver, 1.0e-200);*/
      HYPRE_StructHybridSetConvergenceTol(struct_solver, cf_tol);
      HYPRE_StructHybridSetTwoNorm(struct_solver, 1);
      HYPRE_StructHybridSetRelChange(struct_solver, 0);
      if (solver_type == 2) /* for use with GMRES */
      {
         HYPRE_StructHybridSetStopCrit(struct_solver, 0);
         HYPRE_StructHybridSetKDim(struct_solver, 10);
      }
      HYPRE_StructHybridSetPrintLevel(struct_solver, 1);
      HYPRE_StructHybridSetLogging(struct_solver, 1);
      HYPRE_StructHybridSetSolverType(struct_solver, solver_type);

      if (solver_id == 220)
      {
         /* use symmetric SMG as preconditioner */
         HYPRE_StructSMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSMGSetMemoryUse(struct_precond, 0);
         HYPRE_StructSMGSetMaxIter(struct_precond, 1);
         HYPRE_StructSMGSetTol(struct_precond, 0.0);
         HYPRE_StructSMGSetZeroGuess(struct_precond);
         HYPRE_StructSMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSMGSetLogging(struct_precond, 0);
         HYPRE_StructHybridSetPrecond(struct_solver,
                                      HYPRE_StructSMGSolve,
                                      HYPRE_StructSMGSetup,
                                      struct_precond);
      }

      else if (solver_id == 221)
      {
         /* use symmetric PFMG as preconditioner */
         HYPRE_StructPFMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructPFMGSetMaxIter(struct_precond, 1);
         HYPRE_StructPFMGSetTol(struct_precond, 0.0);
         HYPRE_StructPFMGSetZeroGuess(struct_precond);
         HYPRE_StructPFMGSetRAPType(struct_precond, rap);
         HYPRE_StructPFMGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructPFMGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructPFMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructPFMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructPFMGSetSkipRelax(struct_precond, skip);
         /*HYPRE_StructPFMGSetDxyz(struct_precond, dxyz);*/
         HYPRE_StructPFMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructPFMGSetLogging(struct_precond, 0);
         HYPRE_StructHybridSetPrecond(struct_solver,
                                      HYPRE_StructPFMGSolve,
                                      HYPRE_StructPFMGSetup,
                                      struct_precond);
      }

      else if (solver_id == 222)
      {
         /* use symmetric SparseMSG as preconditioner */
         HYPRE_StructSparseMSGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSparseMSGSetJump(struct_precond, jump);
         HYPRE_StructSparseMSGSetMaxIter(struct_precond, 1);
         HYPRE_StructSparseMSGSetTol(struct_precond, 0.0);
         HYPRE_StructSparseMSGSetZeroGuess(struct_precond);
         HYPRE_StructSparseMSGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructSparseMSGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructSparseMSGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSparseMSGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSparseMSGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSparseMSGSetLogging(struct_precond, 0);
         HYPRE_StructHybridSetPrecond(struct_solver,
                                      HYPRE_StructSparseMSGSolve,
                                      HYPRE_StructSparseMSGSetup,
                                      struct_precond);
      }

      HYPRE_StructHybridSetup(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("Hybrid Solve");
      hypre_BeginTiming(time_index);

      HYPRE_StructHybridSolve(struct_solver, sA, sb, sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_StructHybridGetNumIterations(struct_solver, &num_iterations);
      HYPRE_StructHybridGetFinalRelativeResidualNorm(struct_solver, &final_res_norm);
      HYPRE_StructHybridDestroy(struct_solver);

      if (solver_id == 220)
      {
         HYPRE_StructSMGDestroy(struct_precond);
      }
      else if (solver_id == 221)
      {
         HYPRE_StructPFMGDestroy(struct_precond);
      }
      else if (solver_id == 222)
      {
         HYPRE_StructSparseMSGDestroy(struct_precond);
      }
   }

   /*-----------------------------------------------------------
    * Solve the system using GMRES
    *-----------------------------------------------------------*/

   if ((solver_id > 229) && (solver_id < 240))
   {
      time_index = hypre_InitializeTiming("GMRES Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructGMRESCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_GMRESSetMaxIter( (HYPRE_Solver)struct_solver, 100 );
      HYPRE_GMRESSetTol( (HYPRE_Solver)struct_solver, 1.0e-06 );
      HYPRE_GMRESSetRelChange( (HYPRE_Solver)struct_solver, 0 );
      HYPRE_GMRESSetPrintLevel( (HYPRE_Solver)struct_solver, 1 );
      HYPRE_GMRESSetLogging( (HYPRE_Solver)struct_solver, 1 );

      if (solver_id == 230)
      {
         /* use symmetric SMG as preconditioner */
         HYPRE_StructSMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSMGSetMemoryUse(struct_precond, 0);
         HYPRE_StructSMGSetMaxIter(struct_precond, 1);
         HYPRE_StructSMGSetTol(struct_precond, 0.0);
         HYPRE_StructSMGSetZeroGuess(struct_precond);
         HYPRE_StructSMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSMGSetLogging(struct_precond, 0);
         HYPRE_GMRESSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSMGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSMGSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 231)
      {
         /* use symmetric PFMG as preconditioner */
         HYPRE_StructPFMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructPFMGSetMaxIter(struct_precond, 1);
         HYPRE_StructPFMGSetTol(struct_precond, 0.0);
         HYPRE_StructPFMGSetZeroGuess(struct_precond);
         HYPRE_StructPFMGSetRAPType(struct_precond, rap);
         HYPRE_StructPFMGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructPFMGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructPFMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructPFMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructPFMGSetSkipRelax(struct_precond, skip);
         /*HYPRE_StructPFMGSetDxyz(struct_precond, dxyz);*/
         HYPRE_StructPFMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructPFMGSetLogging(struct_precond, 0);
         HYPRE_GMRESSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructPFMGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructPFMGSetup,
                                (HYPRE_Solver)struct_precond);
      }
      else if (solver_id == 232)
      {
         /* use symmetric SparseMSG as preconditioner */
         HYPRE_StructSparseMSGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSparseMSGSetMaxIter(struct_precond, 1);
         HYPRE_StructSparseMSGSetJump(struct_precond, jump);
         HYPRE_StructSparseMSGSetTol(struct_precond, 0.0);
         HYPRE_StructSparseMSGSetZeroGuess(struct_precond);
         HYPRE_StructSparseMSGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructSparseMSGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructSparseMSGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSparseMSGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSparseMSGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSparseMSGSetLogging(struct_precond, 0);
         HYPRE_GMRESSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSparseMSGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSparseMSGSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 237)
      {
         /* use two-step Jacobi as preconditioner */
         HYPRE_StructJacobiCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructJacobiSetMaxIter(struct_precond, 2);
         HYPRE_StructJacobiSetTol(struct_precond, 0.0);
         HYPRE_StructJacobiSetZeroGuess(struct_precond);
         HYPRE_GMRESSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructJacobiSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructJacobiSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 238)
      {
         /* use diagonal scaling as preconditioner */
#ifdef HYPRE_USE_PTHREADS
         for (i = 0; i < hypre_NumThreads; i++)
         {
            struct_precond[i] = NULL;
         }
#else
         struct_precond = NULL;
#endif
         HYPRE_GMRESSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructDiagScale,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructDiagScaleSetup,
                                (HYPRE_Solver)struct_precond);
      }

      HYPRE_GMRESSetup
         ( (HYPRE_Solver)struct_solver, (HYPRE_Matrix)sA, (HYPRE_Vector)sb, 
                                                          (HYPRE_Vector)sx );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("GMRES Solve");
      hypre_BeginTiming(time_index);

      HYPRE_GMRESSolve
         ( (HYPRE_Solver)struct_solver, (HYPRE_Matrix)sA, (HYPRE_Vector)sb, 
                                                          (HYPRE_Vector)sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_GMRESGetNumIterations( (HYPRE_Solver)struct_solver, &num_iterations);
      HYPRE_GMRESGetFinalRelativeResidualNorm( (HYPRE_Solver)struct_solver, &final_res_norm);
      HYPRE_StructGMRESDestroy(struct_solver);

      if (solver_id == 230)
      {
         HYPRE_StructSMGDestroy(struct_precond);
      }
      else if (solver_id == 231)
      {
         HYPRE_StructPFMGDestroy(struct_precond);
      }
      else if (solver_id == 232)
      {
         HYPRE_StructSparseMSGDestroy(struct_precond);
      }
      else if (solver_id == 237)
      {
         HYPRE_StructJacobiDestroy(struct_precond);
      }
   }
   /*-----------------------------------------------------------
    * Solve the system using BiCGTAB
    *-----------------------------------------------------------*/

   if ((solver_id > 239) && (solver_id < 250))
   {
      time_index = hypre_InitializeTiming("BiCGSTAB Setup");
      hypre_BeginTiming(time_index);

      HYPRE_StructBiCGSTABCreate(MPI_COMM_WORLD, &struct_solver);
      HYPRE_BiCGSTABSetMaxIter( (HYPRE_Solver)struct_solver, 100 );
      HYPRE_BiCGSTABSetTol( (HYPRE_Solver)struct_solver, 1.0e-06 );
      HYPRE_BiCGSTABSetPrintLevel( (HYPRE_Solver)struct_solver, 1 );
      HYPRE_BiCGSTABSetLogging( (HYPRE_Solver)struct_solver, 1 );

      if (solver_id == 240)
      {
         /* use symmetric SMG as preconditioner */
         HYPRE_StructSMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSMGSetMemoryUse(struct_precond, 0);
         HYPRE_StructSMGSetMaxIter(struct_precond, 1);
         HYPRE_StructSMGSetTol(struct_precond, 0.0);
         HYPRE_StructSMGSetZeroGuess(struct_precond);
         HYPRE_StructSMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSMGSetLogging(struct_precond, 0);
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSMGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSMGSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 241)
      {
         /* use symmetric PFMG as preconditioner */
         HYPRE_StructPFMGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructPFMGSetMaxIter(struct_precond, 1);
         HYPRE_StructPFMGSetTol(struct_precond, 0.0);
         HYPRE_StructPFMGSetZeroGuess(struct_precond);
         HYPRE_StructPFMGSetRAPType(struct_precond, rap);
         HYPRE_StructPFMGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructPFMGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructPFMGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructPFMGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructPFMGSetSkipRelax(struct_precond, skip);
         /*HYPRE_StructPFMGSetDxyz(struct_precond, dxyz);*/
         HYPRE_StructPFMGSetPrintLevel(struct_precond, 0);
         HYPRE_StructPFMGSetLogging(struct_precond, 0);
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructPFMGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructPFMGSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 242)
      {
         /* use symmetric SparseMSG as preconditioner */
         HYPRE_StructSparseMSGCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructSparseMSGSetMaxIter(struct_precond, 1);
         HYPRE_StructSparseMSGSetJump(struct_precond, jump);
         HYPRE_StructSparseMSGSetTol(struct_precond, 0.0);
         HYPRE_StructSparseMSGSetZeroGuess(struct_precond);
         HYPRE_StructSparseMSGSetRelaxType(struct_precond, relax);
         if (usr_jacobi_weight)
         {
            HYPRE_StructSparseMSGSetJacobiWeight(struct_precond, jacobi_weight);
         }
         HYPRE_StructSparseMSGSetNumPreRelax(struct_precond, n_pre);
         HYPRE_StructSparseMSGSetNumPostRelax(struct_precond, n_post);
         HYPRE_StructSparseMSGSetPrintLevel(struct_precond, 0);
         HYPRE_StructSparseMSGSetLogging(struct_precond, 0);
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSparseMSGSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructSparseMSGSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 247)
      {
         /* use two-step Jacobi as preconditioner */
         HYPRE_StructJacobiCreate(MPI_COMM_WORLD, &struct_precond);
         HYPRE_StructJacobiSetMaxIter(struct_precond, 2);
         HYPRE_StructJacobiSetTol(struct_precond, 0.0);
         HYPRE_StructJacobiSetZeroGuess(struct_precond);
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructJacobiSolve,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructJacobiSetup,
                                (HYPRE_Solver)struct_precond);
      }

      else if (solver_id == 248)
      {
         /* use diagonal scaling as preconditioner */
#ifdef HYPRE_USE_PTHREADS
         for (i = 0; i < hypre_NumThreads; i++)
         {
            struct_precond[i] = NULL;
         }
#else
         struct_precond = NULL;
#endif
         HYPRE_BiCGSTABSetPrecond( (HYPRE_Solver)struct_solver,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructDiagScale,
                                (HYPRE_PtrToSolverFcn) HYPRE_StructDiagScaleSetup,
                                (HYPRE_Solver)struct_precond);
      }

      HYPRE_BiCGSTABSetup
         ( (HYPRE_Solver)struct_solver, (HYPRE_Matrix)sA, (HYPRE_Vector)sb, 
                                                          (HYPRE_Vector)sx );

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Setup phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      time_index = hypre_InitializeTiming("BiCGSTAB Solve");
      hypre_BeginTiming(time_index);

      HYPRE_BiCGSTABSolve
         ( (HYPRE_Solver)struct_solver, (HYPRE_Matrix)sA, (HYPRE_Vector)sb,
                                                          (HYPRE_Vector)sx);

      hypre_EndTiming(time_index);
      hypre_PrintTiming("Solve phase times", MPI_COMM_WORLD);
      hypre_FinalizeTiming(time_index);
      hypre_ClearTiming();

      HYPRE_BiCGSTABGetNumIterations( (HYPRE_Solver)struct_solver, &num_iterations);
      HYPRE_BiCGSTABGetFinalRelativeResidualNorm( (HYPRE_Solver)struct_solver, &final_res_norm);
      HYPRE_StructBiCGSTABDestroy(struct_solver);

      if (solver_id == 240)
      {
         HYPRE_StructSMGDestroy(struct_precond);
      }
      else if (solver_id == 241)
      {
         HYPRE_StructPFMGDestroy(struct_precond);
      }
      else if (solver_id == 242)
      {
         HYPRE_StructSparseMSGDestroy(struct_precond);
      }
      else if (solver_id == 247)
      {
         HYPRE_StructJacobiDestroy(struct_precond);
      }
   }

   /*-----------------------------------------------------------
    * Gather the solution vector
    *-----------------------------------------------------------*/

   HYPRE_SStructVectorGather(x);

   /*-----------------------------------------------------------
    * Print the solution and other info
    *-----------------------------------------------------------*/

   if (print_system)
   {
      HYPRE_SStructVectorPrint("sstruct.out.x", x, 0);
   }

   if (myid == 0 /* begin lobpcg */ && !lobpcgFlag /* end lobpcg */)
   {
      printf("\n");
      printf("Iterations = %d\n", num_iterations);
      printf("Final Relative Residual Norm = %e\n", final_res_norm);
      printf("\n");
   }

   /*-----------------------------------------------------------
    * Finalize things
    *-----------------------------------------------------------*/

   HYPRE_SStructGridDestroy(grid);
   for (s = 0; s < data.nstencils; s++)
   {
      HYPRE_SStructStencilDestroy(stencils[s]);
   }
   hypre_TFree(stencils);
   HYPRE_SStructGraphDestroy(graph);
   HYPRE_SStructMatrixDestroy(A);
   HYPRE_SStructVectorDestroy(b);
   HYPRE_SStructVectorDestroy(x);


   DestroyData(data);

   hypre_TFree(parts);
   hypre_TFree(refine);
   hypre_TFree(distribute);
   hypre_TFree(block);

   hypre_FinalizeMemoryDebug();

   /* Finalize MPI */
   MPI_Finalize();

   return (0);
}