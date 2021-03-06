//  (C) Copyright Eric Niebler 2004.
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

/*
 Revision history:
   25 August 2005 : Initial version.
*/

#include <mlib/tests/_pc_.h>
//#include <boost/test/minimal.hpp>
#include <boost/foreach.hpp>

///////////////////////////////////////////////////////////////////////////////
// define the container types, used by utility.hpp to generate the helper functions
typedef int foreach_container_type[5];
typedef int const foreach_const_container_type[5];
typedef int foreach_value_type;
typedef int &foreach_reference_type;
typedef int const &foreach_const_reference_type;

#include "./utility.hpp"

///////////////////////////////////////////////////////////////////////////////
// define some containers
//
static int my_array[5] = { 1,2,3,4,5 };
static int const (&my_const_array)[5] = my_array;

///////////////////////////////////////////////////////////////////////////////
// test_main
//   
BOOST_AUTO_TEST_CASE( array_byref_r )
{
    // non-const containers by reference
    BOOST_CHECK(sequence_equal_byref_n_r(my_array, "\5\4\3\2\1"));

    // const containers by reference
    BOOST_CHECK(sequence_equal_byref_c_r(my_const_array, "\5\4\3\2\1"));

    // mutate the mutable collections
    mutate_foreach_byref_r(my_array);

    // compare the mutated collections to the actual results
    BOOST_CHECK(sequence_equal_byref_n_r(my_array, "\6\5\4\3\2"));
}
