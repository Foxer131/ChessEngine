// Forces chess/bitboard.hpp to be compiled so its (uncommented) static_assert
// tests are checked when you build chess_core. No runtime role; delete once a
// real core source includes the header.
#include "chess/bitboard.hpp"
