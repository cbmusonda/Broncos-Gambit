// Compile with:  cmake --build build && ctest --test-dir build
// Or let VSCode TestMate discover via CMakeLists.txt

#define GTEST_MODE  // suppress main() so gTest can provide its own

extern "C" {
#include "AlphaBeta_Trial2.c"
}

#include <gtest/gtest.h>
#include <cstring>

// Call init_zobrist() once before any tests run — main() is suppressed
// when GTEST_MODE is defined, so we need to do it here instead.
class EngineSetup : public ::testing::Environment {
public:
    void SetUp() override { init_zobrist(); }
};

const ::testing::Environment* engine_env =
    ::testing::AddGlobalTestEnvironment(new EngineSetup);

// ── Helpers (no longer in the .c file, so defined here) ──────────────────

static void move_to_uci(Move m, char out[6]) {
    out[0] = (char)('a' + (m.from % 8));
    out[1] = (char)('1' + (m.from / 8));
    out[2] = (char)('a' + (m.to   % 8));
    out[3] = (char)('1' + (m.to   / 8));
    if (m.promo) { out[4] = m.promo; out[5] = '\0'; }
    else         { out[4] = '\0'; }
}

static int move_is_legal(const Pos *p, const char *uci) {
    Move ms[320];
    int n = legal_moves(p, ms);
    for (int i = 0; i < n; i++) {
        char buf[6]; move_to_uci(ms[i], buf);
        if (strcmp(buf, uci) == 0) return 1;
    }
    return 0;
}

static void apply_uci_test(Pos *p, const char *uci) {
    Move m;
    m.from  = (uci[1] - '1') * 8 + (uci[0] - 'a');
    m.to    = (uci[3] - '1') * 8 + (uci[2] - 'a');
    m.promo = uci[4] ? uci[4] : 0;
    *p = make_move(p, m);
}

// ── Group 1: FEN / Position Parsing ──────────────────────────────────────

TEST(FenParsing, WhiteMovesFirst) {
    Pos p; pos_start(&p);
    EXPECT_EQ(p.white_to_move, 1);
}

TEST(FenParsing, StartPosPieceCount) {
    Pos p; pos_start(&p);
    int whites = 0, blacks = 0, empty = 0;
    for (int i = 0; i < 64; i++) {
        char c = p.b[i];
        if      (c == '.')             empty++;
        else if (c >= 'A' && c <= 'Z') whites++;
        else                           blacks++;
    }
    EXPECT_EQ(whites, 16);
    EXPECT_EQ(blacks, 16);
    EXPECT_EQ(empty,  32);
}

TEST(FenParsing, SideToMoveAfterE4) {
    Pos p;
    pos_from_fen(&p, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    EXPECT_EQ(p.white_to_move, 0);
}

TEST(FenParsing, PiecePlacement) {
    Pos p;
    pos_from_fen(&p, "7k/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_EQ(p.b[4],  'K'); // white king on e1
    EXPECT_EQ(p.b[63], 'k'); // black king on h8
}

// ── Group 2: Move Generation ──────────────────────────────────────────────

TEST(MoveGen, TwentyMovesFromStart) {
    Pos p; pos_start(&p);
    Move ms[320];
    EXPECT_EQ(legal_moves(&p, ms), 20);
}

TEST(MoveGen, CheckmateZeroMoves) {
    Pos p;
    pos_from_fen(&p, "r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4");
    Move ms[320];
    EXPECT_EQ(legal_moves(&p, ms), 0);
}

TEST(MoveGen, StalemateZeroMoves) {
    Pos p;
    pos_from_fen(&p, "k7/8/1QK5/8/8/8/8/8 b - - 0 1");
    Move ms[320];
    EXPECT_EQ(legal_moves(&p, ms), 0);
}

TEST(MoveGen, E2E4IsLegal) {
    Pos p; pos_start(&p);
    EXPECT_TRUE(move_is_legal(&p, "e2e4"));
}

TEST(MoveGen, PawnCannotGoBackward) {
    Pos p; pos_start(&p);
    EXPECT_FALSE(move_is_legal(&p, "e2e1"));
}

TEST(MoveGen, KnightOpeningMoves) {
    Pos p; pos_start(&p);
    EXPECT_TRUE(move_is_legal(&p, "g1f3"));
    EXPECT_TRUE(move_is_legal(&p, "b1c3"));
}

TEST(MoveGen, RookBlockedAtStart) {
    Pos p; pos_start(&p);
    EXPECT_FALSE(move_is_legal(&p, "a1a3"));
}

TEST(MoveGen, PromotionFourChoices) {
    Pos p;
    pos_from_fen(&p, "7k/4P3/8/8/8/8/8/4K3 w - - 0 1");
    Move ms[320];
    int n = legal_moves(&p, ms);
    int promo_count = 0;
    for (int i = 0; i < n; i++) if (ms[i].promo) promo_count++;
    EXPECT_EQ(promo_count, 4);
}

TEST(MoveGen, KingCannotWalkIntoCheck) {
    Pos p;
    pos_from_fen(&p, "4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_FALSE(move_is_legal(&p, "e1e2"));
}

TEST(MoveGen, KingCannotCaptureDefendedPiece) {
    Pos p;
    pos_from_fen(&p, "5r1k/8/8/8/8/8/8/4Kr2 w - - 0 1");
    EXPECT_FALSE(move_is_legal(&p, "e1f1"));
}

// ── Group 3: Check Detection ──────────────────────────────────────────────

TEST(CheckDetection, RookCheck) {
    Pos p;
    pos_from_fen(&p, "4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_TRUE(in_check(&p, 1));
}

TEST(CheckDetection, QueenCheck) {
    Pos p;
    pos_from_fen(&p, "4q2k/8/8/8/8/8/8/4K3 w - - 0 1");
    EXPECT_TRUE(in_check(&p, 1));
}

TEST(CheckDetection, NotInCheckAtStart) {
    Pos p; pos_start(&p);
    EXPECT_FALSE(in_check(&p, 1));
    EXPECT_FALSE(in_check(&p, 0));
}

TEST(CheckDetection, NotInCheckAfterE4) {
    Pos p; pos_start(&p);
    apply_uci_test(&p, "e2e4");
    EXPECT_FALSE(in_check(&p, 1));
}

// ── Group 4: Static Evaluation ────────────────────────────────────────────

TEST(Evaluation, NearZeroAtStart) {
    Pos p; pos_start(&p);
    EXPECT_GE(evaluate(&p), -50);
    EXPECT_LE(evaluate(&p),  50);
}

TEST(Evaluation, WhiteUpQueen) {
    Pos p; pos_start(&p);
    p.b[59] = '.'; // remove black queen on d8
    EXPECT_GT(evaluate(&p), 800);
}

TEST(Evaluation, BlackUpQueen) {
    Pos p; pos_start(&p);
    p.b[3] = '.'; // remove white queen on d1
    EXPECT_LT(evaluate(&p), -800);
}

TEST(Evaluation, WhiteUpRook) {
    Pos p; pos_start(&p);
    p.b[56] = '.'; // remove black rook on a8
    EXPECT_GT(evaluate(&p), 400);
}

TEST(Evaluation, CenterPawnBetterThanFlank) {
    Pos pe; pos_start(&pe); apply_uci_test(&pe, "e2e4");
    Pos pa; pos_start(&pa); apply_uci_test(&pa, "a2a4");
    EXPECT_GT(evaluate(&pe), evaluate(&pa));
}

// ── Group 5: AlphaBeta Search ─────────────────────────────────────────────

TEST(AlphaBeta, Depth0EqualsEvaluate) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;
    int ab = alpha_beta(&p, 0, -INF, INF, hist, hlen);
    EXPECT_EQ(ab, evaluate(&p));
}

TEST(AlphaBeta, NodesSearchedNonZero) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;
    nodes_searched = 0;
    alpha_beta(&p, 3, -INF, INF, hist, hlen);
    EXPECT_GT(nodes_searched, 0);
}

TEST(AlphaBeta, PruningReducesNodeCount) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;
    nodes_searched = 0;
    alpha_beta(&p, 3, -INF, INF, hist, hlen);
    // brute force depth 3 = ~20^3 = 8000 nodes; alpha-beta should beat that
    EXPECT_LT(nodes_searched, 8000);
}

TEST(AlphaBeta, BestMoveIsLegal) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;
    Move best = find_best_move(&p, hist, hlen, 2);
    char buf[6]; move_to_uci(best, buf);
    EXPECT_TRUE(move_is_legal(&p, buf));
}

TEST(AlphaBeta, MateInOneWhite) {
    Pos p;
    pos_from_fen(&p, "7k/8/8/8/8/8/7R/R6K w - - 0 1");
    u64 hist[MAX_HIST]; int hlen = 0;
    Move best = find_best_move(&p, hist, hlen, 2);
    Pos after = make_move(&p, best);
    Move ms[320];
    int n = legal_moves(&after, ms);
    EXPECT_EQ(n, 0);
    EXPECT_TRUE(in_check(&after, 0));
}

TEST(AlphaBeta, MateInOneBlack) {
    Pos p;
    pos_from_fen(&p, "r6k/8/8/8/8/8/6r1/K6K b - - 0 1");
    u64 hist[MAX_HIST]; int hlen = 0;
    Move best = find_best_move(&p, hist, hlen, 2);
    Pos after = make_move(&p, best);
    Move ms[320];
    int n = legal_moves(&after, ms);
    EXPECT_EQ(n, 0);
    EXPECT_TRUE(in_check(&after, 1));
}

TEST(AlphaBeta, CapturesFreeQueen) {
    Pos p;
    pos_from_fen(&p, "4k3/8/8/8/3q4/8/8/3QK3 w - - 0 1");
    u64 hist[MAX_HIST]; int hlen = 0;
    Move best = find_best_move(&p, hist, hlen, 2);
    char buf[6]; move_to_uci(best, buf);
    EXPECT_EQ(buf[2], 'd');
    EXPECT_EQ(buf[3], '4');
}

TEST(AlphaBeta, AvoidsHangingQueen) {
    Pos p;
    pos_from_fen(&p, "4k3/8/8/4p3/3Q4/8/8/4K3 w - - 0 1");
    u64 hist[MAX_HIST]; int hlen = 0;
    Move best = find_best_move(&p, hist, hlen, 2);
    // queen on d4 = index 27; it should be the piece that moves away
    EXPECT_EQ(best.from, 27);
}

TEST(AlphaBeta, RepetitionAppliesContempt) {
    // Pre-load the position hash — new engine penalises repetition with CONTEMPT
    // reps==1, white to move -> alpha_beta returns -CONTEMPT*2
    Pos p;
    pos_from_fen(&p, "7k/8/8/8/8/8/8/4K3 w - - 0 1");
    u64 hist[MAX_HIST]; int hlen = 0;
    hist[hlen++] = hash_pos(&p);
    int score = alpha_beta(&p, 4, -INF, INF, hist, hlen);
    EXPECT_EQ(score, -CONTEMPT * 2);
}

// ── Group 6: Self-Play Smoke Tests ────────────────────────────────────────

TEST(SelfPlay, TenPliesAllLegal) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;

    for (int ply = 0; ply < 10; ply++) {
        Move ms[320];
        if (legal_moves(&p, ms) == 0) break;
        Move best = find_best_move(&p, hist, hlen, 2);
        char buf[6]; move_to_uci(best, buf);
        ASSERT_TRUE(move_is_legal(&p, buf)) << "Illegal move at ply " << ply;
        if (hlen < MAX_HIST) hist[hlen++] = hash_pos(&p);
        p = make_move(&p, best);
    }
}

TEST(SelfPlay, HundredPliesNoCrash) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;

    for (int ply = 0; ply < 100; ply++) {
        Move ms[320];
        if (legal_moves(&p, ms) == 0) break;
        Move best = find_best_move(&p, hist, hlen, 2);
        if (hlen < MAX_HIST) hist[hlen++] = hash_pos(&p);
        p = make_move(&p, best);
    }
    SUCCEED();
}

TEST(SelfPlay, KingsAlwaysPresent) {
    Pos p; pos_start(&p);
    u64 hist[MAX_HIST]; int hlen = 0;

    for (int ply = 0; ply < 50; ply++) {
        int wk = 0, bk = 0;
        for (int i = 0; i < 64; i++) {
            if (p.b[i] == 'K') wk = 1;
            if (p.b[i] == 'k') bk = 1;
        }
        ASSERT_TRUE(wk) << "White king missing at ply " << ply;
        ASSERT_TRUE(bk) << "Black king missing at ply " << ply;
        Move ms[320];
        if (legal_moves(&p, ms) == 0) break;
        Move best = find_best_move(&p, hist, hlen, 2);
        if (hlen < MAX_HIST) hist[hlen++] = hash_pos(&p);
        p = make_move(&p, best);
    }
}
