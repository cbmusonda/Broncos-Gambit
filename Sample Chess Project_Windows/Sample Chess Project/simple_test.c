#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "engine2.c"

// Helper functions
static void move_to_uci(Move m, char out[6]) {
    out[0] = (char)('a' + (m.from % 8));
    out[1] = (char)('1' + (m.from / 8));
    out[2] = (char)('a' + (m.to   % 8));
    out[3] = (char)('1' + (m.to   / 8));
    if (m.promo) { out[4] = m.promo; out[5] = '\0'; }
    else         { out[4] = '\0'; }
}

static int move_count_legal(const Pos *p) {
    Move ms[256];
    return legal_moves(p, ms);
}

// ────────────────────────── TESTS ──────────────────────────

int test_starting_position() {
    printf("Test: Starting Position... ");
    Pos p;
    pos_start(&p);
    
    assert(p.white_to_move == 1);
    assert(p.b[0] == 'r');   // a1
    assert(p.b[56] == 'R');  // a8
    
    printf("PASS\n");
    return 1;
}

int test_starting_position_move_count() {
    printf("Test: Starting Position Move Count... ");
    Pos p;
    pos_start(&p);
    
    int n = move_count_legal(&p);
    // Starting position has exactly 20 legal moves
    assert(n == 20);
    
    printf("PASS (20 moves)\n");
    return 1;
}

int test_fen_parsing() {
    printf("Test: FEN Parsing... ");
    Pos p;
    pos_from_fen(&p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1");
    
    assert(p.white_to_move == 1);
    int n = move_count_legal(&p);
    assert(n > 0);
    
    printf("PASS\n");
    return 1;
}

int test_pawn_moves() {
    printf("Test: Pawn Moves... ");
    Pos p;
    pos_from_fen(&p, "8/8/8/8/8/8/P7/8 w - - 0 1");
    
    int n = move_count_legal(&p);
    // Pawn on a2 can move to a3 or a4 (2 moves)
    assert(n >= 2);
    
    printf("PASS\n");
    return 1;
}

int test_simple_move_application() {
    printf("Test: Simple Move Application... ");
    Pos p;
    pos_start(&p);
    
    // Make a move: e2-e4
    Move m;
    m.from = 12;  // e2
    m.to = 28;    // e4
    m.promo = 0;
    
    Pos np = make_move(&p, m);
    
    assert(np.white_to_move == 0);
    assert(np.b[28] == 'P');
    assert(np.b[12] == '.');
    
    printf("PASS\n");
    return 1;
}

int test_white_king_in_check() {
    printf("Test: White King in Check... ");
    Pos p;
    pos_from_fen(&p, "4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    
    // White king on e1, black rook on e2
    assert(in_check(&p, 1) == 1);
    
    printf("PASS\n");
    return 1;
}

int test_pawn_attack() {
    printf("Test: Pawn Attack... ");
    Pos p;
    pos_from_fen(&p, "8/8/8/8/3p4/8/8/8 w - - 0 1");
    
    // Black pawn on d4 attacks c3 and e3
    int sq_c3 = 2 * 8 + 2;  // c3
    int sq_e3 = 2 * 8 + 4;  // e3
    
    assert(is_square_attacked(&p, sq_c3, 0) == 1);
    assert(is_square_attacked(&p, sq_e3, 0) == 1);
    
    printf("PASS\n");
    return 1;
}

int test_pawn_promotion() {
    printf("Test: Pawn Promotion... ");
    Pos p;
    pos_from_fen(&p, "8/4P3/8/8/8/8/8/8 w - - 0 1");
    
    Move ms[256];
    int n = legal_moves(&p, ms);
    
    // Should have 4 promotion moves
    assert(n >= 4);
    
    int promo_count = 0;
    for (int i = 0; i < n; i++) {
        if (ms[i].promo != 0) promo_count++;
    }
    assert(promo_count == 4);
    
    printf("PASS\n");
    return 1;
}

int test_stalemate() {
    printf("Test: Stalemate Position... ");
    Pos p;
    pos_from_fen(&p, "k7/8/8/8/8/8/8/K7 b - - 0 1");
    
    Move ms[256];
    int n = legal_moves(&p, ms);
    
    // Black king has no legal moves
    assert(n == 0);
    
    printf("PASS\n");
    return 1;
}

// ────────────────────────── MAIN ──────────────────────────

int main() {
    printf("\n========== Running Engine2 Tests ==========\n\n");
    
    int passed = 0;
    int total = 0;
    
    total++; passed += test_starting_position();
    total++; passed += test_starting_position_move_count();
    total++; passed += test_fen_parsing();
    total++; passed += test_pawn_moves();
    total++; passed += test_simple_move_application();
    total++; passed += test_white_king_in_check();
    total++; passed += test_pawn_attack();
    total++; passed += test_pawn_promotion();
    total++; passed += test_stalemate();
    
    printf("\n========== Results ==========\n");
    printf("Passed: %d/%d\n\n", passed, total);
    
    if (passed == total) {
        printf("✓ All tests passed!\n");
        return 0;
    } else {
        printf("✗ Some tests failed\n");
        return 1;
    }
}
