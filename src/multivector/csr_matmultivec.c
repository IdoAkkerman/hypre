/*BHEADER**********************************************************************
 * Copyright (c) 2008,  Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * This file is part of HYPRE.  See file COPYRIGHT for details.
 *
 * HYPRE is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * $Revision: 1.6 $
 ***********************************************************************EHEADER*/




/******************************************************************************
 *
 * Matvec functions for hypre_CSRMatrix class.
 *
 *****************************************************************************/

#include "csr_multimatvec.h"
#include "seq_mv.h"
#include "seq_multivector.h"
#include <assert.h>

/*--------------------------------------------------------------------------
 * hypre_CSRMatrixMultiMatvec
 *--------------------------------------------------------------------------*/

int
hypre_CSRMatrixMatMultivec(double alpha, hypre_CSRMatrix *A,
                           hypre_Multivector *x, double beta,
                           hypre_Multivector *y)
{
   double *A_data   = hypre_CSRMatrixData(A);
   int    *A_i      = hypre_CSRMatrixI(A);
   int    *A_j      = hypre_CSRMatrixJ(A);
   int    num_rows = hypre_CSRMatrixNumRows(A);
   int    num_cols = hypre_CSRMatrixNumCols(A);
   double *x_data = hypre_MultivectorData(x);
   double *y_data = hypre_MultivectorData(y);
   int    x_size = hypre_MultivectorSize(x);
   int    y_size = hypre_MultivectorSize(y);
   int    num_vectors = hypre_MultivectorNumVectors(x);
   int    *x_active_ind= x->active_indices;
   int    *y_active_ind= y->active_indices;
   int    num_active_vectors = x->num_active_vectors;
   int    i, j, jj, m, ierr = 0, optimize;
   double temp, tempx, xpar=0.7, *xptr, *yptr;
   
   /*---------------------------------------------------------------------
    *  Check for size compatibility.  Matvec returns ierr = 1 if
    *  length of X doesn't equal the number of columns of A,
    *  ierr = 2 if the length of Y doesn't equal the number of rows
    *  of A, and ierr = 3 if both are true.
    *
    *  Because temporary vectors are often used in Matvec, none of 
    *  these conditions terminates processing, and the ierr flag
    *  is informational only.
    *--------------------------------------------------------------------*/
 
    hypre_assert(num_active_vectors == y->num_active_vectors);
    if (num_cols != x_size) ierr = 1;
    if (num_rows != y_size) ierr = 2;
    if (num_cols != x_size && num_rows != y_size) ierr = 3;
    optimize = 0;
    if (num_active_vectors == num_vectors && num_vectors == y->num_vectors)
       optimize = 1;

   /*-----------------------------------------------------------------------
    * Do (alpha == 0.0) computation - RDF: USE MACHINE EPS
    *-----------------------------------------------------------------------*/

    if (alpha == 0.0)
    {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
       for (i = 0; i < num_rows*num_vectors; i++) y_data[i] *= beta;

       return ierr;
    }

   /*-----------------------------------------------------------------------
    * y = (beta/alpha)*y
    *-----------------------------------------------------------------------*/
   
   temp = beta / alpha;
   
   if (temp != 1.0)
   {
      if (temp == 0.0)
      {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
	 for (i = 0; i < num_rows*num_vectors; i++) y_data[i] = 0.0;
      }
      else
      {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
	 for (i = 0; i < num_rows*num_vectors; i++) y_data[i] *= temp;
      }
   }

   /*-----------------------------------------------------------------
    * y += A*x
    *-----------------------------------------------------------------*/

   if ( num_vectors==1 )
   {
      for (i = 0; i < num_rows; i++)
      {
         temp = y_data[i];
         for (jj = A_i[i]; jj < A_i[i+1]; jj++)
            temp += A_data[jj] * x_data[A_j[jj]];
      }
      y_data[i] = temp;
   }
   else
   {
      if (optimize == 0)
      {
         for (i = 0; i < num_rows; i++)
         {     
            for (j=0; j<num_active_vectors; ++j)
            {
               xptr = x_data[x_active_ind[j]*x_size];
               temp = y_data[y_active_ind[j]*y_size+i];
               for (jj = A_i[i]; jj < A_i[i+1]; jj++)
                  temp += A_data[jj] * xptr[A_j[jj]];
               y_data[y_active_ind[j]*y_size+i] = temp;
            }
         }
      }
      else
      {
         for (i = 0; i < num_rows; i++)
         {     
            for (j=0; j<num_vectors; ++j)
            {
               xptr = x_data[j*x_size];
               temp = y_data[j*y_size+i];
               for (jj = A_i[i]; jj < A_i[i+1]; jj++)
                  temp += A_data[jj] * xptr[A_j[jj]];
               y_data[j*y_size+i] = temp;
            }
         }
         /* different version 
         for (j=0; j<num_vectors; ++j)
         {     
            xptr = x_data[j*x_size];
            for (i = 0; i < num_rows; i++)
            {
               temp = y_data[j*y_size+i];
               for (jj = A_i[i]; jj < A_i[i+1]; jj++)
                  temp += A_data[jj] * xptr[A_j[jj]];
               y_data[j*y_size+i] = temp;
            }
         }
         */
      }
   }

   /*-----------------------------------------------------------------
    * y = alpha*y
    *-----------------------------------------------------------------*/

   if (alpha != 1.0)
   {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
      for (i = 0; i < num_rows*num_vectors; i++)
	 y_data[i] *= alpha;
   }
   return ierr;
}

/*--------------------------------------------------------------------------
 * hypre_CSRMatrixMultiMatvecT
 *
 *   Performs y <- alpha * A^T * x + beta * y
 *
 *   From Van Henson's modification of hypre_CSRMatrixMatvec.
 *--------------------------------------------------------------------------*/

int
hypre_CSRMatrixMatMultivecT(double alpha, hypre_CSRMatrix *A,
                            hypre_Multivector *x, double beta,
                            hypre_Multivector *y)
{
   double *A_data    = hypre_CSRMatrixData(A);
   int    *A_i       = hypre_CSRMatrixI(A);
   int    *A_j       = hypre_CSRMatrixJ(A);
   int    num_rows  = hypre_CSRMatrixNumRows(A);
   int    num_cols  = hypre_CSRMatrixNumCols(A);
   double *x_data = hypre_MultivectorData(x);
   double *y_data = hypre_MultivectorData(y);
   int    x_size = hypre_MultivectorSize(x);
   int    y_size = hypre_MultivectorSize(y);
   int    num_vectors = hypre_MultivectorNumVectors(x);
   int    *x_active_ind= x->active_indices;
   int    *y_active_ind= y->active_indices;
   int    num_active_vectors = x->num_active_vectors;
   double temp;
   int    i, jv, jj, size, ierr = 0;

   /*---------------------------------------------------------------------
    *  Check for size compatibility.  MatvecT returns ierr = 1 if
    *  length of X doesn't equal the number of rows of A,
    *  ierr = 2 if the length of Y doesn't equal the number of 
    *  columns of A, and ierr = 3 if both are true.
    *
    *  Because temporary vectors are often used in MatvecT, none of 
    *  these conditions terminates processing, and the ierr flag
    *  is informational only.
    *--------------------------------------------------------------------*/

    hypre_assert(num_active_vectors == y->num_active_vectors);
    if (num_rows != x_size) ierr = 1;
    if (num_cols != y_size) ierr = 2;
    if (num_rows != x_size && num_cols != y_size) ierr = 3;
 
   /*-----------------------------------------------------------------------
    * Do (alpha == 0.0) computation - RDF: USE MACHINE EPS
    *-----------------------------------------------------------------------*/

   if (alpha == 0.0)
   {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
      for (i = 0; i < num_cols*num_vectors; i++) y_data[i] *= beta;
      return ierr;
   }

   /*-----------------------------------------------------------------------
    * y = (beta/alpha)*y
    *-----------------------------------------------------------------------*/

   temp = beta / alpha;
   
   if (temp != 1.0)
   {
      if (temp == 0.0)
      {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
	 for (i = 0; i < num_cols*num_vectors; i++) y_data[i] = 0.0;
      }
      else
      {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
	 for (i = 0; i < num_cols*num_vectors; i++) y_data[i] *= temp;
      }
   }

   /*-----------------------------------------------------------------
    * y += A^T*x
    *-----------------------------------------------------------------*/

   if ( num_vectors==1 )
   {
      for (i = 0; i < num_rows; i++)
      {
         for (jj = A_i[i]; jj < A_i[i+1]; jj++)
            y_data[A_j[jj]] += A_data[jj] * x_data[i];
      }
   }
   else
   {
      for ( jv=0; jv<num_vectors; ++jv )
      {
         for (jj = A_i[i]; jj < A_i[i+1]; jj++)
            y_data[A_j[jj]+jv*y_size] += A_data[jj] * x_data[i+jv*x_size];
      }
   }

   /*-----------------------------------------------------------------
    * y = alpha*y
    *-----------------------------------------------------------------*/

   if (alpha != 1.0)
   {
#define HYPRE_SMP_PRIVATE i
#include "../utilities/hypre_smp_forloop.h"
      for (i = 0; i < num_cols*num_vectors; i++)
	 y_data[i] *= alpha;
   }

   return ierr;
}
