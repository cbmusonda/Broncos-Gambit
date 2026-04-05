#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Minimal UCI engine: first legal move.
// No castling, no en-passant; promotions -> queen only.

typedef struct {
    int from, to;
    char promo;
} Move;

typedef struct {
    char b[64];
    int white_to_move;
} Pos;

/////////////////////////////////////////////////////////////
// --- Repetition detection ---
#define MAX_HISTORY 512

typedef unsigned long long u64;

// Zobrist tables
static u64 ZobristPiece[64][12]; // [square][piece_index]
static u64 ZobristSide;          // XOR in when black to move
static int zobrist_ready = 0;

// Map piece char to 0-11 index
static int piece_index(char pc) {
    // White: P=0 N=1 B=2 R=3 Q=4 K=5
    // Black: p=6 n=7 b=8 r=9 q=10 k=11
    switch (pc) {
        case 'P': return 0; case 'N': return 1; case 'B': return 2;
        case 'R': return 3; case 'Q': return 4; case 'K': return 5;
        case 'p': return 6; case 'n': return 7; case 'b': return 8;
        case 'r': return 9; case 'q': return 10; case 'k': return 11;
        default:  return -1;
    }
}

// Simple LCG for seeding Zobrist numbers without needing stdlib rand
static u64 lcg_rand(u64 *state) {
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state ^ (*state >> 33);
}

static void init_zobrist(void) {
    if (zobrist_ready) return;
    u64 state = 0xdeadbeefcafeULL;
    for (int s = 0; s < 64; s++)
        for (int p = 0; p < 12; p++)
            ZobristPiece[s][p] = lcg_rand(&state);
    ZobristSide = lcg_rand(&state);
    zobrist_ready = 1;
}

static u64 hash_pos(const Pos *p) {
    u64 h = 0;
    for (int i = 0; i < 64; i++) {
        int pi = piece_index(p->b[i]);
        if (pi >= 0) h ^= ZobristPiece[i][pi];
    }
    if (!p->white_to_move) h ^= ZobristSide;
    return h;
}
////////////////////////////////////////////////////////////

static int sq_index(const char *s) {
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    return rank * 8 + file;
}

static void index_to_sq(int idx, char out[3]) {
    out[0] = (char) ('a' + (idx % 8));
    out[1] = (char) ('1' + (idx / 8));
    out[2] = 0;
}

static void pos_from_fen(Pos *p, const char *fen) {
    memset(p->b, '.', 64);
    p->white_to_move = 1;

    char buf[256];
    strncpy(buf, fen, sizeof(buf)-1);
    buf[sizeof(buf) - 1] = 0;

    char *save = NULL;
    char *placement = strtok_r(buf, " ", &save);
    char *stm = strtok_r(NULL, " ", &save);
    if (stm) p->white_to_move = (strcmp(stm, "w") == 0);

    int rank = 7, file = 0;
    for (size_t i = 0; placement && placement[i]; i++) {
        char c = placement[i];
        if (c == '/') {
            rank--;
            file = 0;
            continue;
        }
        if (isdigit((unsigned char) c)) {
            file += c - '0';
            continue;
        }
        int idx = rank * 8 + file;
        if (idx >= 0 && idx < 64) p->b[idx] = c;
        file++;
    }
}

static void pos_start(Pos *p) {
    pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1");
}

static int is_white_piece(char c) { return c >= 'A' && c <= 'Z'; }

static int is_square_attacked(const Pos *p, int sq, int by_white) {
    int r = sq / 8, f = sq % 8;

    // pawns
    if (by_white) {
        if (r > 0 && f > 0 && p->b[(r - 1) * 8 + (f - 1)] == 'P') return 1;
        if (r > 0 && f < 7 && p->b[(r - 1) * 8 + (f + 1)] == 'P') return 1;
    } else {
        if (r < 7 && f > 0 && p->b[(r + 1) * 8 + (f - 1)] == 'p') return 1;
        if (r < 7 && f < 7 && p->b[(r + 1) * 8 + (f + 1)] == 'p') return 1;
    }

    // knights
    static const int nd[8] = {-17, -15, -10, -6, 6, 10, 15, 17};
    for (int i = 0; i < 8; i++) {
        int to = sq + nd[i];
        if (to < 0 || to >= 64) continue;
        int tr = to / 8, tf = to % 8;
        int dr = tr - r;
        if (dr < 0) dr = -dr;
        int df = tf - f;
        if (df < 0) df = -df;
        if (!((dr == 1 && df == 2) || (dr == 2 && df == 1))) continue;
        char pc = p->b[to];
        if (by_white && pc == 'N') return 1;
        if (!by_white && pc == 'n') return 1;
    }

    // sliders
    static const int dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };

    for (int di = 0; di < 8; di++) {
        int df = dirs[di][0], dr = dirs[di][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int idx = cr * 8 + cf;
            char pc = p->b[idx];
            if (pc != '.') {
                int pc_white = is_white_piece(pc);
                if (pc_white == by_white) {
                    char up = (char) toupper((unsigned char) pc);
                    int rook_dir = (di < 4);
                    int bishop_dir = (di >= 4);
                    if (up == 'Q') return 1;
                    if (rook_dir && up == 'R') return 1;
                    if (bishop_dir && up == 'B') return 1;
                    if (up == 'K' && (abs(cr - r) <= 1 && abs(cf - f) <= 1)) return 1;
                }
                break;
            }
            cr += dr;
            cf += df;
        }
    }

    // king adjacency (extra safety)
    for (int rr = r - 1; rr <= r + 1; rr++) {
        for (int ff = f - 1; ff <= f + 1; ff++) {
            if (rr < 0 || rr >= 8 || ff < 0 || ff >= 8) continue;
            if (rr == r && ff == f) continue;
            char pc = p->b[rr * 8 + ff];
            if (by_white && pc == 'K') return 1;
            if (!by_white && pc == 'k') return 1;
        }
    }

    return 0;
}

static int in_check(const Pos *p, int white_king) {
    char k = white_king ? 'K' : 'k';
    int ksq = -1;
    for (int i = 0; i < 64; i++) if (p->b[i] == k) {
        ksq = i;
        break;
    }
    if (ksq < 0) return 1;
    return is_square_attacked(p, ksq, !white_king);
}

static Pos make_move(const Pos *p, Move m) {
    Pos np = *p;
    char piece = np.b[m.from];
    np.b[m.from] = '.';
    char placed = piece;
    if (m.promo && (piece == 'P' || piece == 'p')) {
        placed = is_white_piece(piece)
                     ? (char) toupper((unsigned char) m.promo)
                     : (char) tolower((unsigned char) m.promo);
    }
    np.b[m.to] = placed;

    // --- CASTLING MOVE ROOK --- //////////////////////////////////////
    if (piece == 'K' && m.from == 4) {
        if (m.to == 6) { // king side
            np.b[7] = '.';
            np.b[5] = 'R';
        } else if (m.to == 2) { // queen side
            np.b[0] = '.';
            np.b[3] = 'R';
        }
    }
    if (piece == 'k' && m.from == 60) {
        if (m.to == 62) {
            np.b[63] = '.';
            np.b[61] = 'r';
        } else if (m.to == 58) {
            np.b[56] = '.';
            np.b[59] = 'r';
        }
    }
    ////////////////////////////////////////////////////////////////

    np.white_to_move = !p->white_to_move;
    return np;
}

static void add_move(Move *moves, int *n, int from, int to, char promo) {
    moves[*n].from = from;
    moves[*n].to = to;
    moves[*n].promo = promo;
    (*n)++;
}

static void gen_pawn(const Pos *p, int from, int white, Move *moves, int *n) 
{ //////////////////////////////////////
    int row    = from / 8;
    int column = from % 8;
    int dir, start_row, promo_row;

    if (white) { dir =  1; start_row = 1; promo_row = 6; }
    else       { dir = -1; start_row = 6; promo_row = 1; }

    // Single push
    int to = from + dir * 8;
    if (to >= 0 && to < 64 && p->b[to] == '.') {
        if (row == promo_row) {
            char promos[4] = {'q','r','b','n'};
            for (int i = 0; i < 4; i++)
                add_move(moves, n, from, to, promos[i]);
        } else {
            add_move(moves, n, from, to, 0);
            if (row == start_row) {
                int to2 = from + dir * 16;
                if (to2 >= 0 && to2 < 64 && p->b[to2] == '.')
                    add_move(moves, n, from, to2, 0);
            }
        }
    }

    // Captures
    int cap_cols[2] = { column - 1, column + 1 };
    for (int i = 0; i < 2; i++) {
        int cc = cap_cols[i];
        if (cc < 0 || cc > 7) continue;

        int cap_sq = from + dir * 8 + (cc - column);
        if (cap_sq < 0 || cap_sq >= 64) continue;
        char target = p->b[cap_sq];
        if (target == '.' || is_white_piece(target) == white) continue;

        if (row == promo_row) {
            char promos[4] = {'q','r','b','n'};
            for (int j = 0; j < 4; j++)
                add_move(moves, n, from, cap_sq, promos[j]); // FIX: was 'to'
        } else {
            add_move(moves, n, from, cap_sq, 0);
        }
    }
    ///////////////////////////////////////////////////////
}

static void gen_knight(const Pos *p, int from, int white, Move *moves, int *n) {
    /////////////////////////////////////////////////////
    static const int nd[8] = {-17, -15, -10, -6, 6, 10, 15, 17};
    int from_col = from % 8;
    int from_row = from / 8;

    for (int i = 0; i < 8; i++) {
        int to = from + nd[i];
        if (to < 0 || to > 63) continue;

        int to_col = to % 8;
        int to_row = to / 8;
        int col_distance = abs(from_col - to_col);
        int row_distance = abs(from_row - to_row);

        if (!((col_distance == 1 && row_distance == 2) ||
              (col_distance == 2 && row_distance == 1))) continue;

        char target = p->b[to];
        // FIX: was shadowing the 'white' parameter with a new local var
        if (target != '.' && is_white_piece(target) == white) continue;

        add_move(moves, n, from, to, 0);
    }
    //////////////////////////////////////////////////////////
}
static void gen_queen(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int r = from / 8;
    int f = from % 8;
    //converting row and column

   //loop through directions
    for (int i = 0; i < dcount; i++) {
        int df = dirs[i][0]; //change left/right
        int dr = dirs[i][1]; //change up/down
    
        int cr = r + dr; //current row, move 1 box in the direction
        int cf = f + df; //current column, move 1 box in the direction

        //stay within the board so move till it hits an edge or piece
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int to = cr * 8 + cf; //convert back to index
            char target = p->b[to]; //check if theres a piece on the square

            if (target == '.') { //empty square
                add_move(moves, n, from, to, 0); //add move and keep going
            } else {
                if (is_white_piece(target) != white) { //enemy piece, can capture but stop after
                    add_move(moves, n, from, to, 0); 
                }
                break; //stop after hitting any piece, whether we captured or not
            }
            //move further in the same direction
            cr += dr;
            cf += df;
        }
    }
}

static void gen_bishop(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int from_file = from % 8;
    int from_rank = from / 8;

    for (int d = 0; d < dcount; d++){
        int df = dirs[d][0];
        int dr = dirs[d][1];

        int cf = from_file + df;
        int cr = from_rank + dr;

        while(cf >= 0 && cf < 8 && cr >= 0 && cr < 8){
            int to = cr * 8 + cf;
            char target = p->b[to];

            if(target == '.'){
                add_move(moves, n, from, to, 0);
            }
            else{
                if(is_white_piece(target) != white){
                     add_move(moves, n, from, to, 0);   
                }
                break;
            }
            cf += df;
            cr += dr;
        }
    }
}

static void gen_rook(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int from_file = from % 8;
    int from_rank = from / 8;

    // loop through each direction the rook can slide (up, down, left, right)
    for (int d = 0; d < dcount; d++) {
        int df = dirs[d][0]; // file delta
        int dr = dirs[d][1]; // rank delta

        int cf = from_file + df;
        int cr = from_rank + dr;

        // keep going until we hit the edge of the board
        while (cf >= 0 && cf < 8 && cr >= 0 && cr < 8) {
            int to = cr * 8 + cf;
            char target = p->b[to];

            if (target == '.') {
                // empty square, rook can move here, keep sliding
                add_move(moves, n, from, to, 0);
            } else {
                // hit a piece, capture if its the enemy then stop either way
                if (is_white_piece(target) != white) {
                    add_move(moves, n, from, to, 0);
                }
                break;
            }

            cf += df;
            cr += dr;
        }
    }
}

static void gen_king(const Pos *p, int from, int white, Move *moves, int *n) {
    /////////////////////////////////////////////////////
    static const int dirs[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},
        {1,1},{1,-1},{-1,1},{-1,-1}
    };

    int from_row = from / 8;
    int from_col = from % 8;

    for (int i = 0; i < 8; i++) {
        int to_row = from_row + dirs[i][1];
        int to_col = from_col + dirs[i][0];
        if (to_row < 0 || to_row > 7 || to_col < 0 || to_col > 7) continue;
        int to = to_row * 8 + to_col;
        char target = p->b[to];
        if (target != '.' && is_white_piece(target) == white) continue;
        add_move(moves, n, from, to, 0);
    }

    // Castling — FIX: moved OUTSIDE the direction loop, no in_check() call here.
    // in_check() during search would recurse into legal_moves -> explosion.
    // We only check square vacancy and rook presence; legality filter in
    // legal_moves() will reject castling moves that pass through check.
    if (white && from == 4) {
        if (p->b[5] == '.' && p->b[6] == '.' && p->b[7] == 'R')
            add_move(moves, n, 4, 6, 0);
        if (p->b[1] == '.' && p->b[2] == '.' && p->b[3] == '.' && p->b[0] == 'R')
            add_move(moves, n, 4, 2, 0);
    }
    if (!white && from == 60) {
        if (p->b[61] == '.' && p->b[62] == '.' && p->b[63] == 'r')
            add_move(moves, n, 60, 62, 0);
        if (p->b[57] == '.' && p->b[58] == '.' && p->b[59] == '.' && p->b[56] == 'r')
            add_move(moves, n, 60, 58, 0);
    }
    ////////////////////////////////////////////////
}

///////////////////////////////////////////////////////////////
/* Piece values in centipawns */
static const int PIECE_VAL[26] = {
    ['P'-'A'] = 100,
    ['N'-'A'] = 320,
    ['B'-'A'] = 330,
    ['R'-'A'] = 500,
    ['Q'-'A'] = 900,
    ['K'-'A'] = 20000
};

/* Piece-square bonuses (white's perspective, a1=index 0) */
static const int PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
static const int PST_KNIGHT[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};
static const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};
static const int PST_ROOK[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};
static const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};
static const int PST_KING[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};

static int pst_lookup(char up, int sq_white_pov) {
    switch (up) {
        case 'P': return PST_PAWN[sq_white_pov];
        case 'N': return PST_KNIGHT[sq_white_pov];
        case 'B': return PST_BISHOP[sq_white_pov];
        case 'R': return PST_ROOK[sq_white_pov];
        case 'Q': return PST_QUEEN[sq_white_pov];
        case 'K': return PST_KING[sq_white_pov];
        default:  return 0;
    }
}

/* Returns score in centipawns from WHITE's perspective */
static int evaluate(const Pos *p) {
    int score = 0;
    for (int i = 0; i < 64; i++) {
        char pc = p->b[i];
        if (pc == '.') continue;
        int white = is_white_piece(pc);
        char up = (char)toupper((unsigned char)pc);
        int idx = up - 'A';
        if (idx < 0 || idx >= 7) continue;

        /* PST: white uses rank as-is; black mirrors vertically */
        int sq_pov = white ? i : (7 - i/8)*8 + (i%8);
        int val = PIECE_VAL[idx] + pst_lookup(up, sq_pov);
        score += white ? val : -val;
    }
    return score;
}
//////////////////////////////////////////////////////////////

static int pseudo_legal_moves(const Pos *p, Move *moves) {
    int n = 0;
    int us_white = p->white_to_move;
    for (int i = 0; i < 64; i++) {
        char pc = p->b[i];
        if (pc == '.') continue;
        int white = is_white_piece(pc);
        if (white != us_white) continue;
        char up = (char) toupper((unsigned char) pc);
        if (up == 'P') gen_pawn(p, i, white, moves, &n);
        else if (up == 'N') gen_knight(p, i, white, moves, &n);
        else if (up == 'B') {
            static const int d[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            gen_bishop(p, i, white, d, 4, moves, &n);
        } else if (up == 'R') {
            static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            gen_rook(p, i, white, d, 4, moves, &n);
        } else if (up == 'Q') {
            static const int d[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            gen_queen(p, i, white, d, 8, moves, &n);
        } else if (up == 'K') gen_king(p, i, white, moves, &n);
    }
    return n;
}

static int legal_moves(const Pos *p, Move *out) {
    Move tmp[256];
    int pn = pseudo_legal_moves(p, tmp);
    int n = 0;
    for (int i = 0; i < pn; i++) {
        Pos np = make_move(p, tmp[i]);
        // after move, side who just moved is !np.white_to_move
        if (!in_check(&np, !np.white_to_move)) {
            out[n++] = tmp[i];
        }
    }
    return n;
}

static void apply_uci_move(Pos *p, const char *uci) {
    if (!uci || strlen(uci) < 4) return;
    Move m;
    m.from = sq_index(uci);
    m.to = sq_index(uci + 2);
    m.promo = (strlen(uci) >= 5) ? uci[4] : 0;
    Pos np = make_move(p, m);
    *p = np;
}

static void parse_position(Pos *p, const char *line) {
    // position startpos [moves ...]
    // position fen <6 fields> [moves ...]
    char buf[1024];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf) - 1] = 0;

    char *toks[128];
    int nt = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t\r\n", &save); tok && nt < 128; tok = strtok_r(NULL, " \t\r\n", &save)) {
        toks[nt++] = tok;
    }

    int i = 1;
    if (i < nt && strcmp(toks[i], "startpos") == 0) {
        pos_start(p);
        i++;
    } else if (i < nt && strcmp(toks[i], "fen") == 0) {
        i++;
        char fen[512] = {0};
        for (int k = 0; k < 6 && i < nt; k++, i++) {
            if (k)
                strcat(fen, " ");
            strcat(fen, toks[i]);
        }
        pos_from_fen(p, fen);
    }

    if (i < nt && strcmp(toks[i], "moves") == 0) {
        i++;
        for (; i < nt; i++) apply_uci_move(p, toks[i]);
    }
}

static void print_bestmove(Move m) {
    char a[3], b[3];
    index_to_sq(m.from, a);
    index_to_sq(m.to, b);
    if (m.promo) printf("bestmove %s%s%c\n", a, b, m.promo);
    else printf("bestmove %s%s\n", a, b);
    fflush(stdout);
}
/////////////////////////////////////////////////////////////////
#define INF 1000000
#define MAX_DEPTH 6

static int nodes_searched; /* optional: for diagnostics */

static int alpha_beta(const Pos *p, int depth, int alpha, int beta,
                      u64 *history, int hist_len) {
    nodes_searched++;

    // --- Repetition: if this position was seen before, it's a draw ---
    u64 h = hash_pos(p);
    for (int i = 0; i < hist_len; i++) {
        if (history[i] == h) return 0;
    }

    Move ms[256];
    int n = legal_moves(p, ms);

    if (n == 0) {
        if (in_check(p, p->white_to_move))
            return p->white_to_move ? -INF : INF;
        return 0; // stalemate
    }

    if (depth == 0)
        return evaluate(p);

    // --- Move ordering: score captures higher so they're tried first ---
    // Simple insertion sort by capture value
    int scores[256];
    for (int i = 0; i < n; i++) {
        char victim = p->b[ms[i].to];
        if (victim != '.') {
            int vi = victim - 'A';
            if (vi >= 0 && vi < 26)
                scores[i] = PIECE_VAL[vi]; // MVV: most valuable victim first
            else
                scores[i] = 0;
        } else {
            scores[i] = 0;
        }
    }
    // Insertion sort descending
    for (int i = 1; i < n; i++) {
        Move tm = ms[i]; int ts = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j] < ts) {
            ms[j+1] = ms[j]; scores[j+1] = scores[j]; j--;
        }
        ms[j+1] = tm; scores[j+1] = ts;
    }

    // Push current position onto history
    history[hist_len] = h;

    if (p->white_to_move) {
        int best = -INF;
        for (int i = 0; i < n; i++) {
            Pos np = make_move(p, ms[i]);
            int score = alpha_beta(&np, depth - 1, alpha, beta,
                                   history, hist_len + 1);
            if (score > best) best = score;
            if (score > alpha) alpha = score;
            if (alpha >= beta) break;
        }
        return best;
    } else {
        int best = INF;
        for (int i = 0; i < n; i++) {
            Pos np = make_move(p, ms[i]);
            int score = alpha_beta(&np, depth - 1, alpha, beta,
                                   history, hist_len + 1);
            if (score < best) best = score;
            if (score < beta) beta = score;
            if (alpha >= beta) break;
        }
        return best;
    }
}

static Move find_best_move(const Pos *p, u64 *game_history, int game_hist_len) {
    Move ms[256];
    int n = legal_moves(p, ms);
    if (n == 0) { Move m = {0,0,0}; return m; }

    // Copy game history into a search buffer with room to grow
    u64 search_history[MAX_HISTORY];
    int shl = game_hist_len < MAX_HISTORY ? game_hist_len : MAX_HISTORY - 1;
    for (int i = 0; i < shl; i++) search_history[i] = game_history[i];

    Move best = ms[0];
    int best_score = p->white_to_move ? -INF : INF;
    nodes_searched = 0;

    for (int i = 0; i < n; i++) {
        Pos np = make_move(p, ms[i]);
        int score = alpha_beta(&np, MAX_DEPTH - 1, -INF, INF,
                               search_history, shl);
        if (p->white_to_move ? (score > best_score) : (score < best_score)) {
            best_score = score;
            best = ms[i];
        }
    }
    return best;
}
////////////////////////////////////////////////////////////////
int main(void) {
init_zobrist();  // <-- add this

    Pos pos;
    pos_start(&pos);

    // Game-level history for repetition detection across actual moves played
    u64 game_history[MAX_HISTORY];
    int game_hist_len = 0;

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (!len) continue;

        if (strcmp(line, "uci") == 0) {
            printf("id name team_c\n");
            printf("id author team_c_bryan\n");
            printf("uciok\n");
            fflush(stdout);
        } else if (strcmp(line, "isready") == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(line, "ucinewgame") == 0) {
            pos_start(&pos);
            game_hist_len = 0;  // reset history on new game
        } else if (strncmp(line, "position", 8) == 0) {
            parse_position(&pos, line);
            // Rebuild game history from the current position hash
            // (Arena resends full position each turn so we snapshot it)
            if (game_hist_len < MAX_HISTORY)
                game_history[game_hist_len++] = hash_pos(&pos);
        } else if (strncmp(line, "go", 2) == 0) {
            Move best = find_best_move(&pos, game_history, game_hist_len);
            Move ms[256];
            int n = legal_moves(&pos, ms);
            if (n <= 0) {
                printf("bestmove 0000\n");
            } else {
                print_bestmove(best);
            }
            fflush(stdout);
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }
    return 0;
}