/*BHEADER**********************************************************************
 * Copyright (c) 2008,  Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * This file is part of HYPRE.  See file COPYRIGHT for details.
 *
 * HYPRE is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * $Revision: 2.9 $
 ***********************************************************************EHEADER*/



/******************************************************************************
 *
 * HYPRE_StructVector interface
 *
 *****************************************************************************/

#include "headers.h"
#include "fortran.h"

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorCreate
 *--------------------------------------------------------------------------*/

void
hypre_F90_IFACE(hypre_structvectorcreate, HYPRE_STRUCTVECTORCREATE)( int      *comm,
                                           long int *grid,
                                           long int *vector,
                                           int      *ierr    )
{
   *ierr = (int)
      ( HYPRE_StructVectorCreate( (MPI_Comm)             *comm,
                                  (HYPRE_StructGrid)     *grid,
                                  (HYPRE_StructVector *) vector   ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorDestroy
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectordestroy, HYPRE_STRUCTVECTORDESTROY)( long int *vector,
                                            int      *ierr   )
{
   *ierr = (int) ( HYPRE_StructVectorDestroy( (HYPRE_StructVector) *vector ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorInitialize
 *--------------------------------------------------------------------------*/

void
hypre_F90_IFACE(hypre_structvectorinitialize, HYPRE_STRUCTVECTORINITIALIZE)( long int *vector,
                                               int      *ierr   )
{
   *ierr = (int)
      ( HYPRE_StructVectorInitialize( (HYPRE_StructVector) *vector ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorSetValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectorsetvalues, HYPRE_STRUCTVECTORSETVALUES)( long int *vector,
                                              int      *grid_index,
                                              double   *values,
                                              int      *ierr       )
{
   *ierr = (int)
      ( HYPRE_StructVectorSetValues( (HYPRE_StructVector) *vector,
                                     (int *)              grid_index,
                                     (double)             *values     ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorSetBoxValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectorsetboxvalues, HYPRE_STRUCTVECTORSETBOXVALUES)( long int *vector,
                                                 int      *ilower,
                                                 int      *iupper,
                                                 double   *values,
                                                 int      *ierr   )
{
   *ierr = (int)
      ( HYPRE_StructVectorSetBoxValues( (HYPRE_StructVector) *vector,
                                        (int *)              ilower,
                                        (int *)              iupper,
                                        (double *)           values  ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorAddToValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectoraddtovalues, HYPRE_STRUCTVECTORADDTOVALUES)( long int *vector,
                                                int      *grid_index,
                                                double   *values,
                                                int      *ierr       )
{
   *ierr = (int)
      ( HYPRE_StructVectorAddToValues( (HYPRE_StructVector) *vector,
                                       (int *)              grid_index,
                                       (double)             *values     ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorAddToBoxValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectoraddtoboxvalue, HYPRE_STRUCTVECTORADDTOBOXVALUE)( long int *vector,
                                                   int      *ilower,
                                                   int      *iupper,
                                                   double   *values,
                                                   int      *ierr   )
{
   *ierr = (int)
      ( HYPRE_StructVectorAddToBoxValues( (HYPRE_StructVector) *vector,
                                          (int *)              ilower,
                                          (int *)              iupper,
                                          (double *)           values  ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorScaleValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectorscalevalues, HYPRE_STRUCTVECTORSCALEVALUES)
                                             ( long int *vector,
                                               double   *factor,
                                               int      *ierr       )
{
   *ierr = (int)
      ( HYPRE_StructVectorScaleValues( (HYPRE_StructVector) *vector,
                                       (double)             *factor ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorGetValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectorgetvalues, HYPRE_STRUCTVECTORGETVALUES)( long int *vector,
                                              int      *grid_index,
                                              double   *values_ptr,
                                              int      *ierr       )
{
   *ierr = (int)
      ( HYPRE_StructVectorGetValues( (HYPRE_StructVector) *vector,
                                     (int *)              grid_index,
                                     (double *)           values_ptr ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorGetBoxValues
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectorgetboxvalues, HYPRE_STRUCTVECTORGETBOXVALUES)( long int *vector,
                                                 int      *ilower,
                                                 int      *iupper,
                                                 double   *values,
                                                 int      *ierr   )
{
   *ierr = (int)
      ( HYPRE_StructVectorGetBoxValues( (HYPRE_StructVector) *vector,
                                        (int *)              ilower,
                                        (int *)              iupper,
                                        (double *)           values  ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorAssemble
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectorassemble, HYPRE_STRUCTVECTORASSEMBLE)( long int *vector,
                                             int      *ierr   )
{
   *ierr = (int)
      ( HYPRE_StructVectorAssemble( (HYPRE_StructVector) *vector ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorSetNumGhost
 *--------------------------------------------------------------------------*/
 
void
hypre_F90_IFACE(hypre_structvectorsetnumghost, HYPRE_STRUCTVECTORSETNUMGHOST)( long int *vector,
                                                int      *num_ghost,
                                                int      *ierr      )
{
   *ierr = (int)
      ( HYPRE_StructVectorSetNumGhost( (HYPRE_StructVector) *vector,
                                       (int *)              num_ghost ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorCopy
 *--------------------------------------------------------------------------*/
 
void
hypre_F90_IFACE(hypre_structvectorcopy, HYPRE_STRUCTVECTORCOPY)
                                              ( long int *x,
                                                long int *y,
                                                int      *ierr )
{
   *ierr = (int) ( HYPRE_StructVectorCopy( (HYPRE_StructVector) *x,
                                           (HYPRE_StructVector) *y ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorSetConstantValues
 *--------------------------------------------------------------------------*/

void
hypre_F90_IFACE(hypre_structvectorsetconstantva, HYPRE_STRUCTVECTORSETCONSTANTVA)
                                               ( long int *vector,
                                                  double   *values,
                                                  int      *ierr   )
{
   *ierr = (int)
      ( HYPRE_StructVectorSetConstantValues( (HYPRE_StructVector)  *vector,
                                             (double)              *values ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorGetMigrateCommPkg
 *--------------------------------------------------------------------------*/

void
hypre_F90_IFACE(hypre_structvectorgetmigratecom, HYPRE_STRUCTVECTORGETMIGRATECOM)( long int *from_vector,
                                                  long int *to_vector,
                                                  long int *comm_pkg,
                                                  int      *ierr        )
{
   *ierr = (int)
      ( HYPRE_StructVectorGetMigrateCommPkg( (HYPRE_StructVector) *from_vector,
                                             (HYPRE_StructVector) *to_vector,
                                             (HYPRE_CommPkg *)    comm_pkg    ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorMigrate
 *--------------------------------------------------------------------------*/

void 
hypre_F90_IFACE(hypre_structvectormigrate, HYPRE_STRUCTVECTORMIGRATE)( long int *comm_pkg,
                                            long int *from_vector,
                                            long int *to_vector,
                                            int      *ierr        )
{
   *ierr = (int)
      ( HYPRE_StructVectorMigrate( (HYPRE_CommPkg)      *comm_pkg,
                                   (HYPRE_StructVector) *from_vector,
                                   (HYPRE_StructVector) *to_vector   ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_CommPkgDestroy
 *--------------------------------------------------------------------------*/

void
hypre_F90_IFACE(hypre_destroycommpkg, HYPRE_DESTROYCOMMPKG)( long int *comm_pkg,
                                       int      *ierr     )
{
   *ierr = (int)
      ( HYPRE_CommPkgDestroy( (HYPRE_CommPkg) *comm_pkg ) );
}

/*--------------------------------------------------------------------------
 * HYPRE_StructVectorPrint
 *--------------------------------------------------------------------------*/

void
hypre_F90_IFACE(hypre_structvectorprint, HYPRE_STRUCTVECTORPRINT)(
   long int  *vector,
   int       *all,
   int       *ierr )
{
   *ierr = (int)
      ( HYPRE_StructVectorPrint("HYPRE_StructVector.out",
                                (HYPRE_StructVector) *vector,
                                (int)                *all) );
}