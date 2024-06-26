// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2017 - 2020 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------

#ifndef dealii_differentiation_ad_ad_number_types_h
#define dealii_differentiation_ad_ad_number_types_h


#include <deal.II/base/config.h>


DEAL_II_NAMESPACE_OPEN

namespace Differentiation
{
  namespace AD
  {
    /**
     * An enumeration to indicate which type of auto-differentiable number
     * is to be used for computations. If a type that is selected for use
     * is not available in the library, a run-time error will be thrown.
     */
    enum class NumberTypes
    {
      /**
       * A dummy type for floating point numbers (i.e., non-differentiable
       * scalar types).
       *
       * This option exists to facilitate the use of template meta-programming
       * techniques to select, based on this enumeration, which
       * auto-differentiable number type will be used to perform calculations.
       * It will not permit any computations because the underlying number types
       * resulting from its selection are floating point types, rather than
       * auto-differentiable numbers.
       */
      none,
      /**
       * Taped forward and reverse-mode ADOL-C number type (n-differentiable).
       *
       * First derivatives will be computed using reverse mode, while the second
       * derivatives will be computed using forward mode. Even higher-order
       * derivatives can be computed using ADOL-C's own driver functions.
       */
      adolc_taped,

      /**
       * Tapeless dynamic forward-mode ADOL-C number type (once differentiable).
       */
      adolc_tapeless,

      /**
       * Tapeless dynamic forward-mode Sacado number type (once differentiable).
       */
      sacado_dfad,

      /**
       * Tapeless nested dynamic forward-mode Sacado number type (twice
       * differentiable).
       *
       * Both the first and second derivatives will be computed using forward
       * mode.
       */
      sacado_dfad_dfad,

      /**
       * Tapeless reverse-mode Sacado number type (once differentiable).
       */
      sacado_rad,

      /**
       * Tapeless nested reverse-mode and dynamic forward-mode Sacado number
       * type (twice differentiable).
       *
       * First derivatives will be computed using reverse mode, while the second
       * derivatives will be computed using forward mode.
       *
       * Note that the repeated use of the nested reverse-forward mode results
       * in a memory leak described in this <a
       * href="https://github.com/trilinos/Trilinos/issues/7741"> Trilinos
       * issue.</a>
       */
      sacado_rad_dfad
    };

  } // namespace AD
} // namespace Differentiation



DEAL_II_NAMESPACE_CLOSE

#endif
