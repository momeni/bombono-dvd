// Boost.Range library
//
//  Copyright Neil Groves 2009. Use, modification and
//  distribution is subject to the Boost Software License, Version
//  1.0. (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
//
// For more information, see http://www.boost.org/libs/range/
//
#include <mlib/tests/_pc_.h>

#include <mlib/range/adaptor/indexed.hpp>
//#include <mlib/range/algorithm/copy.hpp>
//#include <mlib/range/algorithm_ext/push_back.hpp>

#include <boost/assign.hpp>
#include <algorithm>
#include <iostream>
#include <vector>

namespace 
{
    template<class Iterator>
    void display_element_and_index(Iterator first, Iterator last)
    {
        for (Iterator it = first; it != last; ++it)
        {
            std::cout << "Element = " << *it << " Index = " << it.index() << std::endl;
        }
    }

    template<class SinglePassRange>
    void display_element_and_index(const SinglePassRange& rng)
    {
        display_element_and_index(boost::begin(rng), boost::end(rng));
    }

    template<class Iterator1, class Iterator2>
    void check_element_and_index(
            Iterator1 test_first,
            Iterator1 test_last,
            Iterator2 reference_first,
            Iterator2 reference_last)
    {
        BOOST_CHECK_EQUAL( std::distance(test_first, test_last),
                           std::distance(reference_first, reference_last) );

        int reference_index = 0;

        Iterator1 test_it = test_first;
        Iterator2 reference_it = reference_first;
        for (; test_it != test_last; ++test_it, ++reference_it, ++reference_index)
        {
            BOOST_CHECK_EQUAL( *test_it, *reference_it );
            BOOST_CHECK_EQUAL( test_it.index(), reference_index );
        }
    }

    template<class SinglePassRange1, class SinglePassRange2>
    void check_element_and_index(
        const SinglePassRange1& test_rng,
        const SinglePassRange2& reference_rng)
    {
        check_element_and_index(boost::begin(test_rng), boost::end(test_rng),
            boost::begin(reference_rng), boost::end(reference_rng));
    }

    void indexed_example_test()
    {
        using namespace boost::assign;
        using namespace boost::adaptors;

        std::vector<int> input;
        input += 10,20,30,40,50,60,70,80,90;

        //display_element_and_index( input | indexed(0) );

        check_element_and_index(
            input | indexed(0),
            input);
    }
}

BOOST_AUTO_TEST_CASE( test_range_indexed_example_test )
{
    indexed_example_test();
}

