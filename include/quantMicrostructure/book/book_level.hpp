#ifndef QUANTMICROSTRUCTURE_BOOK_BOOK_LEVEL_HPP
#define QUANTMICROSTRUCTURE_BOOK_BOOK_LEVEL_HPP

#include "quantMicrostructure/core/types.hpp"

namespace quantMicrostructure
{

    /// One price level of one side of a book: the total quantity resting there.
    struct BookLevel
    {
        Price price{};
        Quantity qty{};

        friend bool operator==(const BookLevel &, const BookLevel &) = default;
    };

} // namespace quantMicrostructure

#endif
