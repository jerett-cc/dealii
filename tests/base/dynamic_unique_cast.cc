// ------------------------------------------------------------------------
//
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2018 - 2020 by the deal.II authors
//
// This file is part of the deal.II library.
//
// Part of the source code is dual licensed under Apache-2.0 WITH
// LLVM-exception OR LGPL-2.1-or-later. Detailed license information
// governing the source code and code contributions can be found in
// LICENSE.md and CONTRIBUTING.md at the top level directory of deal.II.
//
// ------------------------------------------------------------------------


// test dynamic_unique_cast

#include "../tests.h"

class B
{
public:
  virtual ~B()
  {}
};

class D : public B
{};


void
test()
{
  // Create a pointer to D
  std::unique_ptr<D> d = std::make_unique<D>();

  // See that we can successfully downcast to B
  std::unique_ptr<B> b = Utilities::dynamic_unique_cast<B>(std::move(d));

  // Ownership now rests with b, but it's still a D. Verify this:
  Assert(d.get() == nullptr, ExcInternalError());
  Assert(b.get() != nullptr, ExcInternalError());
  Assert(dynamic_cast<D *>(b.get()) != nullptr, ExcInternalError());

  // Check that we can again upcast to D:
  std::unique_ptr<D> dd = Utilities::dynamic_unique_cast<D>(std::move(b));

  // Ownership now rests with b, but it's still a D. Verify this:
  Assert(b.get() == nullptr, ExcInternalError());
  Assert(dd.get() != nullptr, ExcInternalError());

  deallog << "OK" << std::endl;
}


void
invalid_test()
{
  // Create a pointer to B
  std::unique_ptr<B> b = std::make_unique<B>();

  // Check that we can indeed not upcast to D:
  try
    {
      std::unique_ptr<D> dd = Utilities::dynamic_unique_cast<D>(std::move(b));
    }
  catch (const std::bad_cast &)
    {
      deallog << "OK" << std::endl;
    }
}



int
main()
{
  initlog();

  test();
  invalid_test();
}
