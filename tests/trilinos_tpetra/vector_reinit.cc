// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2016 - 2024 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------

// run LinearAlgebra::TpetraWrappers::Vector<double>.renit() in MPI environment
// to test IndexSet::make_trilinos_map(), which contained an MPI-related bug.
// Namely, a Fortran type MPI_LOGICAL was used instead of MPI_INT.


#include <deal.II/base/index_set.h>

#include <deal.II/distributed/grid_refinement.h>
#include <deal.II/distributed/tria.h>

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_q.h>

#include <deal.II/grid/grid_generator.h>

#include <deal.II/lac/trilinos_tpetra_vector.h>

#include "../tests.h"


static const unsigned int dim = 2;

void
test()
{
  MPI_Comm mpi_communicator(MPI_COMM_WORLD);

  parallel::distributed::Triangulation<dim> tria(
    mpi_communicator,
    typename Triangulation<dim>::MeshSmoothing(
      Triangulation<dim>::smoothing_on_refinement |
      Triangulation<dim>::smoothing_on_coarsening));

  GridGenerator::hyper_cube(tria, -1, 0);
  tria.refine_global(2);

  const unsigned int poly_degree = 1;
  FE_Q<dim>          fe(poly_degree);

  DoFHandler<dim> dof_handler(tria);
  dof_handler.distribute_dofs(fe);

  IndexSet locally_owned_dofs = dof_handler.locally_owned_dofs();

  LinearAlgebra::TpetraWrappers::Vector<double, MemorySpace::Default> vector_Re;
  vector_Re.reinit(locally_owned_dofs, mpi_communicator);

  deallog << "OK" << std::endl;
}


int
main(int argc, char *argv[])
{
  initlog();
  deallog.depth_console(0);

  Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);
  {
    test();
  }
}
