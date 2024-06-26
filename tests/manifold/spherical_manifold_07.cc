// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2016 - 2020 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------

// Check that SphericalManifold also works in a stable manner
// if two points are almost collinear with the center and another one
// with a vanishing weight is located in the other hemisphere.

#include <deal.II/base/quadrature.h>
#include <deal.II/base/utilities.h>

#include <deal.II/grid/manifold_lib.h>

#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/vector.h>

#include "../tests.h"


int
main()
{
  initlog();
  Point<3>                      center(0.0, 0.0, 0.0);
  const SphericalManifold<3, 3> manifold(center);

  for (unsigned int i = 1; i < 20; ++i)
    {
      const double eps = 1 / std::pow(10, i * 1.0);

      Point<3> P1(-eps, std::sqrt(1. - eps * eps), 0.);
      Point<3> P2(-eps, -std::sqrt(1. - eps * eps), 0.);
      Point<3> P3(1., 0., 0.);

      std::vector<Point<3>> points1(3);
      std::vector<double>   weights(3);

      points1[0] = P1;
      points1[1] = P2;
      points1[2] = P3;

      weights[0] = .5 - eps;
      weights[1] = .5 - eps;
      weights[2] = 2 * eps;

      Point<3> Q = manifold.get_new_point(make_array_view(points1),
                                          make_array_view(weights));

      deallog << Q << std::endl;
    }

  return 0;
}
