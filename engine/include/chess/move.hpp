#pragma once
// =============================================================================
// chess/move.hpp - a move packed into 16 bits.
//
//   bit:  15 14 | 13 12 | 11 10  9  8  7  6 | 5  4  3  2  1  0
//          type  | promo |       from        |        to
//
//   to    : destination square (0..63), bits 0..5
//   from  : origin square      (0..63), bits 6..11
//   promo : promotion piece stored as (pieceType - KNIGHT) so it fits in 2 bits
//           (KNIGHT->0, BISHOP->1, ROOK->2, QUEEN->3), bits 12..13
//   type  : NORMAL / PROMOTION / EN_PASSANT / CASTLING, bits 14..15. The values
//           below are already shifted into place, so packing just ORs them in.
//
// Fill in make() and the four decoders (look for `// TODO`), then uncomment the
// test block at the bottom and build chess_core to check it.
// =============================================================================

#include <cstdint>
#include "chess/types.hpp"

namespace chess {

// Move type lives in bits 14..15; values are pre-shifted into that position.
enum MoveType : int {
    NORMAL     = 0,
    PROMOTION  = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING   = 3 << 14
};

class Move {
public:
    Move() = default;
    constexpr explicit Move(std::uint16_t data) : data_(data) {}

    // Build a move. For promotions pass type=PROMOTION and the promo piece;
    // for en passant / castling pass the matching type (promo is ignored).
    static constexpr Move make(
        Square from, 
        Square to,
        MoveType type = NORMAL, 
        PieceType promo = KNIGHT
    ) {
        // TODO: combine four fields into one 16-bit value and wrap it in a Move:
        //   - `to`    occupies bits 0..5   (no shift)
        //   - `from`  occupies bits 6..11  (shift it up)
        //   - `promo` is stored as (promo - KNIGHT) in bits 12..13 (shift it up)
        //   - `type`  is already shifted into bits 14..15, so OR it in as-is
        // OR them all together, then `return Move(<that value>)`.
        uint16_t p_from = static_cast<uint16_t>(from) << 6;
        uint16_t p_promo = static_cast<uint16_t>(promo - KNIGHT) << 12;

        return Move(static_cast<uint16_t>(type) | p_promo | p_from | static_cast<uint16_t>(to));
    }

    // bits 0..5
    constexpr Square to_sq() const {
        return Square(data_ & 0x3F);
    }

    // bits 6..11
    constexpr Square from_sq() const {
        return Square((data_ >> 6) & 0x3F);
    }

    // bits 14..15 - kept in place (MoveType values are already shifted there).
    constexpr MoveType type_of() const {
        return MoveType(data_ & 0xC000);
    }

    // bits 12..13, undoing the (pieceType - KNIGHT) we stored.
    constexpr PieceType promotion_type() const {
        return PieceType(((data_ >> 12) & 0x3) + KNIGHT);
    }

    constexpr std::uint16_t raw() const { return data_; }
    constexpr bool operator==(const Move&) const = default;

private:
    std::uint16_t data_ = 0;
};

// The "no move" sentinel. A real move never has from == to, so 0 is safe.
inline constexpr Move MOVE_NONE{};

} // namespace chess
