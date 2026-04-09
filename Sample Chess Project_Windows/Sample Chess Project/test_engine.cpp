// Compile with:  cmake --build build && ctest --test-dir build
// Or let VSCode TestMate discover via CMakeLists.txt

#define GTEST_MODE  // suppress main() so gTest can provide its own
#define main engine2_main_not_used  // Rename main in engine2.c

extern "C" {
#include "engine2.c"
}

#include <gtest/gtest.h>
#include <cstring>

// ── Tests ──────────────────────────────────────────────────────────────────

TEST(PositionSetup, StartPosition) {
    Pos p;
    pos_start(&p);
    
    EXPECT_EQ(p.white_to_move, 1);
    EXPECT_EQ(p.b[0], 'R');   // a1 = white rook  
    EXPECT_EQ(p.b[7], 'R');   // h1 = white rook
    EXPECT_EQ(p.b[56], 'r');  // a8 = black rook
    EXPECT_EQ(p.b[63], 'r');  // h8 = black rook
}

TEST(PositionSetup, FenParsing) {
    Pos p;
    pos_from_fen(&p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1");
    
    EXPECT_EQ(p.white_to_move, 1);
    EXPECT_EQ(p.b[0], 'R');
    Move ms[256];
    int n = legal_moves(&p, ms);
    EXPECT_GT(n, 0);
}

TEST(MoveGeneration, StartingPositionMoveCount) {
    Pos p;
    pos_start(&p);
    
    Move ms[256];
    int n = legal_moves(&p, ms);
    EXPECT_EQ(n, 20);
}

TEST(MoveGeneration, PawnMoves) {
    Pos p;
    pos_from_fen(&p, "k7/8/8/8/8/8/P7/K7 w - - 0 1");
    
    Move ms[256];
    int n = legal_moves(&p, ms);
    EXPECT_GT(n, 0);
}

TEST(MoveApplication, SimpleMove) {
    Pos p;
    pos_start(&p);
    
    Move m;
    m.from = 12;  // e2
    m.to = 28;    // e4
    m.promo = 0;
    
    Pos np = make_move(&p, m);
    
    EXPECT_EQ(np.white_to_move, 0);
    EXPECT_EQ(np.b[28], 'P');
    EXPECT_EQ(np.b[12], '.');
}

TEST(Check, WhiteKingInCheck) {
    Pos p;
    pos_from_fen(&p, "4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    EXPECT_TRUE(in_check(&p, 1));
}

TEST(Check, BlackKingInCheck) {
    Pos p;
    pos_from_fen(&p, "4k3/8/8/8/8/8/4R3/4K3 b - - 0 1");
    EXPECT_TRUE(in_check(&p, 0));
}

TEST(SquareAttack, PawnAttack) {
    Pos p;
    pos_from_fen(&p, "8/8/8/8/3p4/8/8/8 w - - 0 1");
    
    int sq_c3 = 2 * 8 + 2;
    int sq_e3 = 2 * 8 + 4;
    
    EXPECT_TRUE(is_square_attacked(&p, sq_c3, 0));
    EXPECT_TRUE(is_square_attacked(&p, sq_e3, 0));
}

TEST(Promotion, PawnPromotion) {
    Pos p;
    pos_from_fen(&p, "6k1/4P3/8/8/8/8/8/6K1 w - - 0 1");
    
    Move ms[256];
    int n = legal_moves(&p, ms);
    EXPECT_GT(n, 0);
}

TEST(EndgameScenarios, SimpleBoardState) {
    Pos p;
    pos_from_fen(&p, "7k/8/8/8/8/8/8/K6 b - - 0 1");
    
    Move ms[256];
    int n = legal_moves(&p, ms);
    EXPECT_GT(n, 0);
}
