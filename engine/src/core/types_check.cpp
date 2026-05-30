// Compilation unit that forces chess/types.hpp to be compiled, so its
// (uncommented) acceptance-test static_asserts are actually checked when you
// build the chess_core target. This file has no runtime role and can be deleted
// once real core sources include the header.
#include "chess/types.hpp"
