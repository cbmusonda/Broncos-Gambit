/*
 * AlphaBetaNew.c  —  Improved engine for Broncos Gambit
 *
 * Based on AlphaBeta_Trial2.c.  Key improvements over the baseline:
 *   1.  Quiescence search — captures + promotions searched at leaf nodes
 *       with stand-pat / alpha-beta bounds (resolves tactical noise)
 *   2.  Passed-pawn evaluation — rank bonus, connected-passer bonus,
 *       blocked-passer penalty, bonus scales up in endgame
 *   3.  King safety — pawn shield score, weighted by game phase
 *   4.  Early queen development penalty — when undeveloped minors remain
 *   5.  Game-phase interpolation — opening PST <-> endgame king PST
 *   6.  Killer-move heuristic + history heuristic for move ordering
 *   7.  Transposition table — hash / depth / bound / score / best move
 *   8.  Negamax search  (cleaner than minimax for TT + heuristics)
 *   9.  Castling legality fix — king may not castle through an attacked square
 *  10.  En-passant rejection — tournament variant forbids en-passant;
 *       diagonal pawn captures onto empty squares are silently ignored
 *
 * UCI commands supported: uci, isready, ucinewgame, position, go, quit
 * No en-passant is generated or accepted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef unsigned long long u64;
typedef struct { int from, to; char promo; } Move;
typedef struct { char b[64]; int white_to_move; } Pos;

/* ═══════════════════════════════════════════════════════════════
 *  Zobrist hashing  (identical to Trial2 — same seed for compat)
 * ═══════════════════════════════════════════════════════════════ */
static u64 ZP[64][12], ZS;
static int zr = 0;

static int piece_index(char c) {
    switch (c) {
        case 'P': return 0; case 'N': return 1; case 'B': return 2;
        case 'R': return 3; case 'Q': return 4; case 'K': return 5;
        case 'p': return 6; case 'n': return 7; case 'b': return 8;
        case 'r': return 9; case 'q': return 10; case 'k': return 11;
        default:  return -1;
    }
}
static u64 lcg(u64 *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s ^ (*s >> 33);
}
static void init_zobrist(void) {
    if (zr) return;
    u64 s = 0xdeadbeefcafeULL;
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 12; j++) ZP[i][j] = lcg(&s);
    ZS = lcg(&s); zr = 1;
}
static u64 hash_pos(const Pos *p) {
    u64 h = 0;
    for (int i = 0; i < 64; i++) { int pi = piece_index(p->b[i]); if (pi >= 0) h ^= ZP[i][pi]; }
    if (!p->white_to_move) h ^= ZS;
    return h;
}

/* ═══════════════════════════════════════════════════════════════
 *  Board utilities
 * ═══════════════════════════════════════════════════════════════ */
static int  sq_index(const char *s)        { return (s[1]-'1')*8 + (s[0]-'a'); }
static void index_to_sq(int i, char o[3]) { o[0]='a'+i%8; o[1]='1'+i/8; o[2]=0; }
static int  is_white_piece(char c)         { return c >= 'A' && c <= 'Z'; }

static void pos_from_fen(Pos *p, const char *fen) {
    memset(p->b, '.', 64); p->white_to_move = 1;
    char buf[256]; strncpy(buf, fen, 255); buf[255] = 0;
    char *sv = NULL;
    char *pl  = strtok_r(buf, " ", &sv);
    char *stm = strtok_r(NULL, " ", &sv);
    if (stm) p->white_to_move = (strcmp(stm,"w") == 0);
    int rank = 7, file = 0;
    for (size_t i = 0; pl && pl[i]; i++) {
        char c = pl[i];
        if (c == '/') { rank--; file = 0; }
        else if (isdigit((unsigned char)c)) file += c - '0';
        else { int idx = rank*8+file; if (idx>=0&&idx<64) p->b[idx]=c; file++; }
    }
}
static void pos_start(Pos *p) {
    pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1");
}

/* ═══════════════════════════════════════════════════════════════
 *  Attack detection  (unchanged from Trial2)
 * ═══════════════════════════════════════════════════════════════ */
static int is_square_attacked(const Pos *p, int sq, int by_white) {
    int r = sq/8, f = sq%8;
    /* Pawn attacks */
    if (by_white) {
        if (r>0 && f>0 && p->b[(r-1)*8+(f-1)]=='P') return 1;
        if (r>0 && f<7 && p->b[(r-1)*8+(f+1)]=='P') return 1;
    } else {
        if (r<7 && f>0 && p->b[(r+1)*8+(f-1)]=='p') return 1;
        if (r<7 && f<7 && p->b[(r+1)*8+(f+1)]=='p') return 1;
    }
    /* Knight attacks */
    static const int nd[8] = {-17,-15,-10,-6,6,10,15,17};
    for (int i = 0; i < 8; i++) {
        int to = sq + nd[i]; if (to<0||to>=64) continue;
        int dr = abs(to/8-r), df = abs(to%8-f);
        if (!((dr==1&&df==2)||(dr==2&&df==1))) continue;
        char pc = p->b[to];
        if (by_white && pc=='N') return 1;
        if (!by_white && pc=='n') return 1;
    }
    /* Sliding + king */
    static const int dirs[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int di = 0; di < 8; di++) {
        int df2=dirs[di][0], dr2=dirs[di][1], cr=r+dr2, cf=f+df2;
        while (cr>=0&&cr<8&&cf>=0&&cf<8) {
            int idx = cr*8+cf; char pc = p->b[idx];
            if (pc != '.') {
                if (is_white_piece(pc) == by_white) {
                    char up = (char)toupper((unsigned char)pc);
                    if (up=='Q') return 1;
                    if (di<4 && up=='R') return 1;
                    if (di>=4 && up=='B') return 1;
                    if (up=='K' && abs(cr-r)<=1 && abs(cf-f)<=1) return 1;
                }
                break;
            }
            cr += dr2; cf += df2;
        }
    }
    /* King proximity */
    for (int rr=r-1; rr<=r+1; rr++) for (int ff=f-1; ff<=f+1; ff++) {
        if (rr<0||rr>=8||ff<0||ff>=8||(rr==r&&ff==f)) continue;
        char pc = p->b[rr*8+ff];
        if (by_white && pc=='K') return 1;
        if (!by_white && pc=='k') return 1;
    }
    return 0;
}
static int in_check(const Pos *p, int wk) {
    char k = wk ? 'K' : 'k';
    for (int i = 0; i < 64; i++) if (p->b[i]==k) return is_square_attacked(p,i,!wk);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  Make move  (unchanged from Trial2)
 * ═══════════════════════════════════════════════════════════════ */
static Pos make_move(const Pos *p, Move m) {
    Pos np = *p; char pc = np.b[m.from]; np.b[m.from] = '.';
    char placed = pc;
    if (m.promo && (pc=='P'||pc=='p'))
        placed = is_white_piece(pc) ? (char)toupper((unsigned char)m.promo)
                                    : (char)tolower((unsigned char)m.promo);
    np.b[m.to] = placed;
    if (pc=='K' && m.from==4)  { if(m.to==6){np.b[7]='.';np.b[5]='R';} else if(m.to==2){np.b[0]='.';np.b[3]='R';} }
    if (pc=='k' && m.from==60) { if(m.to==62){np.b[63]='.';np.b[61]='r';} else if(m.to==58){np.b[56]='.';np.b[59]='r';} }
    np.white_to_move = !p->white_to_move;
    return np;
}
static void add_move(Move *mv, int *n, int f, int t, char pr) {
    mv[*n].from=f; mv[*n].to=t; mv[*n].promo=pr; (*n)++;
}

/* ═══════════════════════════════════════════════════════════════
 *  Move generation — NO EN-PASSANT
 *
 *  Diagonal pawn captures are only generated when the target square
 *  holds an actual enemy piece.  Empty-square diagonal captures
 *  (en-passant style) are never generated by this engine.
 * ═══════════════════════════════════════════════════════════════ */
static void gen_pawn(const Pos *p, int from, int white, Move *mv, int *n) {
    int row=from/8, col=from%8, dir, sr, pr2;
    if (white) { dir=1; sr=1; pr2=6; } else { dir=-1; sr=6; pr2=1; }

    /* Forward push */
    int to = from + dir*8;
    if (to>=0 && to<64 && p->b[to]=='.') {
        if (row==pr2) { char ps[4]={'q','r','b','n'}; for(int i=0;i<4;i++) add_move(mv,n,from,to,ps[i]); }
        else {
            add_move(mv,n,from,to,0);
            if (row==sr) { int t2=from+dir*16; if(t2>=0&&t2<64&&p->b[t2]=='.') add_move(mv,n,from,t2,0); }
        }
    }

    /* Diagonal captures — ONLY onto occupied enemy squares (no en-passant) */
    int caps[2] = {col-1, col+1};
    for (int i = 0; i < 2; i++) {
        int cc = caps[i]; if (cc<0||cc>7) continue;
        int csq = from + dir*8 + (cc-col); if (csq<0||csq>=64) continue;
        char tgt = p->b[csq];
        /* Reject empty square — this is the en-passant guard */
        if (tgt == '.' || is_white_piece(tgt)==white) continue;
        if (row==pr2) { char ps[4]={'q','r','b','n'}; for(int j=0;j<4;j++) add_move(mv,n,from,csq,ps[j]); }
        else add_move(mv,n,from,csq,0);
    }
}

static void gen_knight(const Pos *p, int from, int white, Move *mv, int *n) {
    static const int nd[8]={-17,-15,-10,-6,6,10,15,17}; int fc=from%8, fr=from/8;
    for (int i=0;i<8;i++) {
        int to=from+nd[i]; if(to<0||to>63) continue;
        if(!((abs(fc-to%8)==1&&abs(fr-to/8)==2)||(abs(fc-to%8)==2&&abs(fr-to/8)==1))) continue;
        char tgt=p->b[to]; if(tgt!='.'&&is_white_piece(tgt)==white) continue;
        add_move(mv,n,from,to,0);
    }
}
static void gen_slider(const Pos *p, int from, int white, const int dirs[][2], int dc, Move *mv, int *n) {
    int r=from/8, f=from%8;
    for (int d=0;d<dc;d++) {
        int cr=r+dirs[d][1], cf=f+dirs[d][0];
        while (cr>=0&&cr<8&&cf>=0&&cf<8) {
            int to=cr*8+cf; char tgt=p->b[to];
            if (tgt=='.') add_move(mv,n,from,to,0);
            else { if(is_white_piece(tgt)!=white) add_move(mv,n,from,to,0); break; }
            cr+=dirs[d][1]; cf+=dirs[d][0];
        }
    }
}

static void gen_king(const Pos *p, int from, int white, Move *mv, int *n) {
    static const int dirs[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    int fr=from/8, fc=from%8;
    for (int i=0;i<8;i++) {
        int tr=fr+dirs[i][1], tc=fc+dirs[i][0]; if(tr<0||tr>7||tc<0||tc>7) continue;
        int to=tr*8+tc; char tgt=p->b[to];
        if(tgt!='.'&&is_white_piece(tgt)==white) continue;
        add_move(mv,n,from,to,0);
    }
    /*
     * Castling — IMPROVED over Trial2:
     * The king must not be in check, must not pass through an attacked square,
     * and must not end in check (the last condition is enforced by legal_moves).
     */
    if (white && from==4) {
        /* Kingside: e1→g1, rook on h1 */
        if (p->b[5]=='.' && p->b[6]=='.' && p->b[7]=='R' &&
            !is_square_attacked(p,4,0) &&   /* e1 not attacked */
            !is_square_attacked(p,5,0) &&   /* f1 not attacked */
            !is_square_attacked(p,6,0))     /* g1 not attacked */
            add_move(mv,n,4,6,0);
        /* Queenside: e1→c1, rook on a1 */
        if (p->b[3]=='.' && p->b[2]=='.' && p->b[1]=='.' && p->b[0]=='R' &&
            !is_square_attacked(p,4,0) &&   /* e1 not attacked */
            !is_square_attacked(p,3,0) &&   /* d1 not attacked */
            !is_square_attacked(p,2,0))     /* c1 not attacked */
            add_move(mv,n,4,2,0);
    }
    if (!white && from==60) {
        /* Kingside: e8→g8, rook on h8 */
        if (p->b[61]=='.' && p->b[62]=='.' && p->b[63]=='r' &&
            !is_square_attacked(p,60,1) &&
            !is_square_attacked(p,61,1) &&
            !is_square_attacked(p,62,1))
            add_move(mv,n,60,62,0);
        /* Queenside: e8→c8, rook on a8 */
        if (p->b[59]=='.' && p->b[58]=='.' && p->b[57]=='.' && p->b[56]=='r' &&
            !is_square_attacked(p,60,1) &&
            !is_square_attacked(p,59,1) &&
            !is_square_attacked(p,58,1))
            add_move(mv,n,60,58,0);
    }
}

static int pseudo_legal_moves(const Pos *p, Move *mv) {
    int n=0;
    for (int i=0;i<64;i++) {
        char pc=p->b[i]; if(pc=='.') continue;
        int white=is_white_piece(pc); if(white!=p->white_to_move) continue;
        char up=(char)toupper((unsigned char)pc);
        if(up=='P') gen_pawn(p,i,white,mv,&n);
        else if(up=='N') gen_knight(p,i,white,mv,&n);
        else if(up=='B') { static const int d[4][2]={{1,1},{1,-1},{-1,1},{-1,-1}}; gen_slider(p,i,white,d,4,mv,&n); }
        else if(up=='R') { static const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}}; gen_slider(p,i,white,d,4,mv,&n); }
        else if(up=='Q') { static const int d[8][2]={{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}}; gen_slider(p,i,white,d,8,mv,&n); }
        else if(up=='K') gen_king(p,i,white,mv,&n);
    }
    return n;
}
static int legal_moves(const Pos *p, Move *out) {
    Move tmp[320]; int pn=pseudo_legal_moves(p,tmp), n=0;
    for (int i=0;i<pn;i++) { Pos np=make_move(p,tmp[i]); if(!in_check(&np,!np.white_to_move)) out[n++]=tmp[i]; }
    return n;
}

/*
 * apply_uci_move — parse and apply a UCI move string.
 *
 * En-passant guard: if a pawn moves diagonally to an EMPTY square, that is
 * an en-passant-style capture which this tournament variant forbids.
 * We silently ignore such moves rather than corrupting the board state.
 */
static void apply_uci_move(Pos *p, const char *uci) {
    if (!uci || strlen(uci) < 4) return;
    Move m; m.from=sq_index(uci); m.to=sq_index(uci+2); m.promo=strlen(uci)>=5?uci[4]:0;
    /* Reject en-passant: pawn diagonal onto empty square */
    char pc = p->b[m.from];
    if ((pc=='P'||pc=='p') && (m.to%8 != m.from%8) && p->b[m.to]=='.') return;
    *p = make_move(p, m);
}

/* ═══════════════════════════════════════════════════════════════
 *  Evaluation
 * ═══════════════════════════════════════════════════════════════ */
static const int PIECE_VAL[26] = {
    ['P'-'A']=100, ['N'-'A']=320, ['B'-'A']=330,
    ['R'-'A']=500, ['Q'-'A']=900, ['K'-'A']=20000
};

/* --- Piece-square tables (white perspective, a1=sq 0) --- */
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
/* Opening king: castle and stay safe */
static const int PST_KING_OP[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};
/* Endgame king: centralize */
static const int PST_KING_EG[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

/* Passed-pawn rank bonus (index = rank from own back rank, 0=start, 6=one before promo) */
static const int PASSED_BONUS[8] = { 0, 0, 10, 20, 35, 55, 80, 0 };

/*
 * game_phase — returns 0 (full endgame) to 24 (full opening).
 * Counts remaining major/minor pieces: Q=4, R=2, B/N=1.
 */
static int game_phase(const Pos *p) {
    int ph = 0;
    for (int i = 0; i < 64; i++) {
        char up = (char)toupper((unsigned char)p->b[i]);
        if (up=='Q') ph+=4; else if (up=='R') ph+=2; else if (up=='N'||up=='B') ph+=1;
    }
    return ph > 24 ? 24 : ph;
}

static int pst_blended(char up, int sq, int phase) {
    int op=0, eg=0;
    switch (up) {
        case 'P': return PST_PAWN[sq];
        case 'N': return PST_KNIGHT[sq];
        case 'B': return PST_BISHOP[sq];
        case 'R': return PST_ROOK[sq];
        case 'Q': return PST_QUEEN[sq];
        case 'K': op=PST_KING_OP[sq]; eg=PST_KING_EG[sq]; break;
        default:  return 0;
    }
    return (op * phase + eg * (24 - phase)) / 24;
}

/* Is the pawn on sq (white=1) a passed pawn? */
static int is_passed(const Pos *p, int sq, int white) {
    int rank=sq/8, file=sq%8;
    if (white) {
        for (int r=rank+1; r<8; r++)
            for (int f=file-1; f<=file+1; f++)
                if (f>=0&&f<=7&&p->b[r*8+f]=='p') return 0;
    } else {
        for (int r=rank-1; r>=0; r--)
            for (int f=file-1; f<=file+1; f++)
                if (f>=0&&f<=7&&p->b[r*8+f]=='P') return 0;
    }
    return 1;
}

/* Pawn shield: count friendly pawns on king's file ±1, 1-2 ranks forward */
static int pawn_shield(const Pos *p, int king_sq, int white) {
    int r=king_sq/8, f=king_sq%8, score=0, dir=white?1:-1;
    char pawn=white?'P':'p';
    for (int df=-1; df<=1; df++) {
        int ff=f+df; if (ff<0||ff>7) continue;
        int r1=r+dir, r2=r+dir*2;
        if (r1>=0&&r1<8 && p->b[r1*8+ff]==pawn) score+=15;
        else if (r2>=0&&r2<8 && p->b[r2*8+ff]==pawn) score+=7;
    }
    return score;
}

/* Count minor pieces still on their home squares (development proxy) */
static int undeveloped_minors(const Pos *p, int white) {
    int cnt=0;
    if (white) {
        if(p->b[1]=='N') cnt++; if(p->b[6]=='N') cnt++;
        if(p->b[2]=='B') cnt++; if(p->b[5]=='B') cnt++;
    } else {
        if(p->b[57]=='n') cnt++; if(p->b[62]=='n') cnt++;
        if(p->b[58]=='b') cnt++; if(p->b[61]=='b') cnt++;
    }
    return cnt;
}

/*
 * evaluate — returns score in centipawns, positive = white is better.
 * Components:
 *   - Material + phase-blended PSTs
 *   - Passed-pawn bonus (scales with endgame phase)
 *   - Connected passed-pawn bonus, blocked passed-pawn penalty
 *   - King pawn shield (weighted by opening phase)
 *   - Early queen development penalty
 */
static int evaluate(const Pos *p) {
    int phase = game_phase(p);
    int score = 0;
    int wk_sq = -1, bk_sq = -1;

    /* Material + PST */
    for (int i = 0; i < 64; i++) {
        char pc = p->b[i]; if (pc=='.') continue;
        int white = is_white_piece(pc);
        char up = (char)toupper((unsigned char)pc);
        int idx = up - 'A'; if (idx<0||idx>=26) continue;
        int sq = white ? i : (7 - i/8)*8 + (i%8); /* mirror for black */
        int val = PIECE_VAL[idx] + pst_blended(up, sq, phase);
        score += white ? val : -val;
        if (up=='K') { if(white) wk_sq=i; else bk_sq=i; }
    }

    /* Passed-pawn evaluation */
    for (int i = 0; i < 64; i++) {
        char pc = p->b[i]; if (pc!='P' && pc!='p') continue;
        int white = (pc=='P');
        if (!is_passed(p, i, white)) continue;

        int rank_own = white ? (i/8) : (7 - i/8);  /* rank from own back rank */
        int base = PASSED_BONUS[rank_own];
        /* Scale bonus: double in full endgame */
        int bonus = base + base * (24 - phase) / 24;

        /* Blocked passed pawn: square directly in front is occupied */
        int front = white ? i+8 : i-8;
        if (front>=0 && front<64 && p->b[front]!='.') bonus -= 30;

        /* Connected passed pawn: adjacent file within 1 rank also has a passed pawn */
        int file = i % 8;
        for (int df = -1; df <= 1; df += 2) {
            int ff = file + df; if (ff<0||ff>7) continue;
            int base_r = i/8;
            for (int dr = -1; dr <= 1; dr++) {
                int r2 = base_r + dr; if (r2<0||r2>=8) continue;
                char adj = p->b[r2*8+ff];
                if (adj == (white?'P':'p') && is_passed(p, r2*8+ff, white)) { bonus+=15; break; }
            }
        }

        score += white ? bonus : -bonus;
    }

    /* King safety — pawn shield, weighted by opening phase */
    if (wk_sq >= 0) score += pawn_shield(p, wk_sq, 1) * phase / 24;
    if (bk_sq >= 0) score -= pawn_shield(p, bk_sq, 0) * phase / 24;

    /*
     * Early queen development penalty.
     * If the queen has left its starting square while >= 2 minor pieces
     * are still undeveloped, penalise.  Only meaningful in the opening.
     */
    {
        int wq_moved = (p->b[3] != 'Q');  /* white queen off d1 */
        int bq_moved = (p->b[59] != 'q'); /* black queen off d8 */
        int w_undv = undeveloped_minors(p, 1);
        int b_undv = undeveloped_minors(p, 0);
        if (wq_moved && w_undv >= 2) score -= 30 * phase / 24;
        if (bq_moved && b_undv >= 2) score += 30 * phase / 24;
    }

    return score;
}

/* ═══════════════════════════════════════════════════════════════
 *  Transposition table
 * ═══════════════════════════════════════════════════════════════ */
#define TT_SIZE   (1 << 20)   /* 1 M entries ≈ 32 MB */
#define TT_MASK   (TT_SIZE - 1)
#define BOUND_NONE  0
#define BOUND_EXACT 1
#define BOUND_LOWER 2   /* score is a lower bound  (beta cutoff) */
#define BOUND_UPPER 3   /* score is an upper bound (failed low)  */

typedef struct { u64 hash; int depth, score, bound; Move best; } TTEntry;
static TTEntry tt[TT_SIZE];

static void tt_store(u64 h, int d, int s, int b, Move best) {
    TTEntry *e = &tt[h & TT_MASK];
    e->hash=h; e->depth=d; e->score=s; e->bound=b; e->best=best;
}
static TTEntry *tt_probe(u64 h) {
    TTEntry *e = &tt[h & TT_MASK];
    return (e->hash==h && e->bound!=BOUND_NONE) ? e : NULL;
}
static void tt_clear(void) { memset(tt, 0, sizeof(tt)); }

/* ═══════════════════════════════════════════════════════════════
 *  Search infrastructure: killers, history, constants
 * ═══════════════════════════════════════════════════════════════ */
#define INF      1000000
#define MAX_PLY  64
#define MAX_HIST 1024
/*
 * CONTEMPT: mild penalty for the side that is about to repeat a position.
 * Lower than Trial2's 200; a small contempt prevents infinite repetition
 * without forcing the engine to sacrifice material to avoid a draw.
 */
#define CONTEMPT 50

static int  nodes_searched;
static Move killers[MAX_PLY][2];
static int  history_tab[64][64];

static void clear_heuristics(void) {
    memset(killers,     0, sizeof(killers));
    memset(history_tab, 0, sizeof(history_tab));
}

/*
 * move_score — priority for move ordering.
 * Order: captures (MVV-LVA) > promotions > killer 1 > killer 2 > history.
 */
static int move_score(const Pos *p, Move m, int ply) {
    char victim   = p->b[m.to];
    char attacker = p->b[m.from];
    if (victim != '.') {
        int vi=(toupper((unsigned char)victim) -'A'), ai=(toupper((unsigned char)attacker)-'A');
        int vv=(vi>=0&&vi<26)?PIECE_VAL[vi]:0, av=(ai>=0&&ai<26)?PIECE_VAL[ai]:0;
        return 10000000 + vv*10 - av; /* MVV-LVA */
    }
    if (m.promo) return 9000000;
    if (ply < MAX_PLY) {
        if (killers[ply][0].from==m.from && killers[ply][0].to==m.to) return 8000000;
        if (killers[ply][1].from==m.from && killers[ply][1].to==m.to) return 7000000;
    }
    return history_tab[m.from][m.to];
}

static void order_moves(const Pos *p, Move *ms, int n, int ply) {
    for (int i=1; i<n; i++) {
        Move tm=ms[i]; int ts=move_score(p,tm,ply), j=i-1;
        while (j>=0 && move_score(p,ms[j],ply)<ts) { ms[j+1]=ms[j]; j--; }
        ms[j+1]=tm;
    }
}

static void store_killer(int ply, Move m) {
    if (ply>=MAX_PLY) return;
    if (killers[ply][0].from==m.from && killers[ply][0].to==m.to) return;
    killers[ply][1]=killers[ply][0];
    killers[ply][0]=m;
}

static int count_reps(const u64 *hist, int len, u64 h) {
    int c=0; for(int i=0;i<len;i++) if(hist[i]==h) c++; return c;
}

/* ═══════════════════════════════════════════════════════════════
 *  Quiescence search  (negamax, captures + promotions only)
 *
 *  Stand-pat: we assume the side to move can always "do nothing",
 *  so if the static eval already beats beta we cut off immediately.
 *  This prevents horizon-effect blunders at leaf nodes.
 * ═══════════════════════════════════════════════════════════════ */
static int quiescence(const Pos *p, int alpha, int beta, int qdepth) {
    nodes_searched++;

    /* Stand-pat score from side-to-move perspective */
    int stand_pat = evaluate(p);
    if (!p->white_to_move) stand_pat = -stand_pat;

    if (stand_pat >= beta) return stand_pat;
    if (stand_pat > alpha) alpha = stand_pat;

    /* Safety cap: prevent quiescence tree explosion */
    if (qdepth <= -8) return stand_pat;

    /* Generate pseudo-legal moves; keep only captures and promotions */
    Move all[320]; int an = pseudo_legal_moves(p, all);
    Move ms[320];  int n  = 0;
    for (int i = 0; i < an; i++)
        if (p->b[all[i].to] != '.' || all[i].promo) ms[n++] = all[i];
    order_moves(p, ms, n, 0);

    for (int i = 0; i < n; i++) {
        Pos np = make_move(p, ms[i]);
        if (in_check(&np, !np.white_to_move)) continue; /* illegal */
        int score = -quiescence(&np, -beta, -alpha, qdepth-1);
        if (score >= beta) return score;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

/* ═══════════════════════════════════════════════════════════════
 *  Alpha-beta search  (negamax)
 *
 *  Returns score from the perspective of the side to move
 *  (positive = current player is winning).
 *
 *  Features: TT probe/store, killer moves, history heuristic,
 *            quiescence at depth 0, repetition draw detection.
 * ═══════════════════════════════════════════════════════════════ */
static int alpha_beta(const Pos *p, int depth, int alpha, int beta,
                      u64 *hist, int hlen, int ply) {
    nodes_searched++;

    /* --- Repetition --- */
    u64 h = hash_pos(p);
    int reps = count_reps(hist, hlen, h);
    if (reps >= 2) return 0;          /* true draw — neither side benefits   */
    if (reps == 1) return -CONTEMPT;  /* mild penalty to discourage repeating */

    /* --- Terminal positions --- */
    Move ms[320]; int n = legal_moves(p, ms);
    if (n == 0) return in_check(p, p->white_to_move) ? (-INF + ply) : 0;

    /* --- Leaf: quiescence --- */
    if (depth <= 0) return quiescence(p, alpha, beta, 0);

    /* --- Transposition table probe --- */
    TTEntry *tte = tt_probe(h);
    Move tt_best = {0,0,0};
    if (tte) {
        tt_best = tte->best;
        if (tte->depth >= depth) {
            if (tte->bound == BOUND_EXACT) return tte->score;
            if (tte->bound == BOUND_LOWER && tte->score > alpha) alpha = tte->score;
            if (tte->bound == BOUND_UPPER && tte->score < beta)  beta  = tte->score;
            if (alpha >= beta) return tte->score;
        }
    }

    /* --- Move ordering: sort, then promote TT best to front --- */
    order_moves(p, ms, n, ply);
    if (tt_best.from != tt_best.to) {
        for (int i = 0; i < n; i++) {
            if (ms[i].from==tt_best.from && ms[i].to==tt_best.to && ms[i].promo==tt_best.promo) {
                Move tmp=ms[0]; ms[0]=ms[i]; ms[i]=tmp; break;
            }
        }
    }

    if (hlen < MAX_HIST) hist[hlen] = h;

    int best_score = -INF;
    Move best_move = ms[0];
    int orig_alpha = alpha;

    for (int i = 0; i < n; i++) {
        Pos np = make_move(p, ms[i]);
        int score = -alpha_beta(&np, depth-1, -beta, -alpha, hist, hlen+1, ply+1);

        if (score > best_score) { best_score = score; best_move = ms[i]; }
        if (score > alpha)        alpha = score;
        if (alpha >= beta) {
            /* Beta cutoff: reward quiet moves with killer/history bonuses */
            if (p->b[ms[i].to] == '.') {
                store_killer(ply, ms[i]);
                history_tab[ms[i].from][ms[i].to] += depth * depth;
            }
            break;
        }
    }

    /* --- Store to TT --- */
    int bound = (best_score <= orig_alpha) ? BOUND_UPPER :
                (best_score >= beta)       ? BOUND_LOWER : BOUND_EXACT;
    tt_store(h, depth, best_score, bound, best_move);

    return best_score;
}

/* ═══════════════════════════════════════════════════════════════
 *  Root search with iterative deepening
 * ═══════════════════════════════════════════════════════════════ */
#define MAX_DEPTH 5

static Move find_best_move(const Pos *p, const u64 *gh, int ghl, int max_depth) {
    Move ms[320]; int n = legal_moves(p, ms);
    if (n == 0) { Move m={0,0,0}; return m; }

    /* Copy game history into local search stack */
    u64 sh[MAX_HIST]; int shl = ghl < MAX_HIST ? ghl : MAX_HIST-1;
    for (int i = 0; i < shl; i++) sh[i] = gh[i];

    /* Avoid creating a threefold repetition at the root */
    Move *slist = ms; int sn = n;
    Move non_rep[320]; int nr = 0;
    if (count_reps(gh, ghl, hash_pos(p)) >= 2) {
        for (int i = 0; i < n; i++) {
            Pos np = make_move(p, ms[i]);
            if (count_reps(gh, ghl, hash_pos(&np)) == 0) non_rep[nr++] = ms[i];
        }
        if (nr > 0) { slist = non_rep; sn = nr; }
    }

    clear_heuristics();
    order_moves(p, slist, sn, 0);
    Move best = slist[0];

    /* Iterative deepening: each pass refines the best move and warms the TT */
    for (int depth = 1; depth <= max_depth; depth++) {
        Move cur_best = slist[0];
        int  cur_score = -INF;
        nodes_searched = 0;

        for (int i = 0; i < sn; i++) {
            Pos np = make_move(p, slist[i]);
            /* Negate: alpha_beta returns from side-to-move (opponent's) view */
            int score = -alpha_beta(&np, depth-1, -INF, INF, sh, shl, 1);
            if (score > cur_score) { cur_score = score; cur_best = slist[i]; }
        }
        best = cur_best;
    }
    return best;
}

/* ═══════════════════════════════════════════════════════════════
 *  UCI position parsing  (identical to Trial2)
 * ═══════════════════════════════════════════════════════════════ */
static void parse_position(Pos *p, const char *line, u64 *gh, int *ghl) {
    char buf[4096]; strncpy(buf, line, 4095); buf[4095]=0;
    char *toks[512]; int nt=0; char *sv=NULL;
    for (char *tok=strtok_r(buf," \t\r\n",&sv); tok&&nt<512; tok=strtok_r(NULL," \t\r\n",&sv))
        toks[nt++]=tok;
    int i=1;
    if (i<nt && strcmp(toks[i],"startpos")==0) { pos_start(p); i++; }
    else if (i<nt && strcmp(toks[i],"fen")==0) {
        i++; char fen[512]={0};
        for (int k=0;k<6&&i<nt;k++,i++) { if(k) strcat(fen," "); strcat(fen,toks[i]); }
        pos_from_fen(p, fen);
    }
    *ghl=0; gh[(*ghl)++]=hash_pos(p);
    if (i<nt && strcmp(toks[i],"moves")==0) {
        i++;
        for (; i<nt; i++) { apply_uci_move(p, toks[i]); if(*ghl<MAX_HIST) gh[(*ghl)++]=hash_pos(p); }
    }
}

static void print_bestmove(Move m) {
    char a[3], b[3]; index_to_sq(m.from,a); index_to_sq(m.to,b);
    if (m.promo) printf("bestmove %s%s%c\n",a,b,m.promo);
    else         printf("bestmove %s%s\n",a,b);
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════════
 *  Main — UCI loop
 * ═══════════════════════════════════════════════════════════════ */
#ifndef GTEST_MODE
int main(void) {
    init_zobrist(); tt_clear();
    Pos pos; pos_start(&pos);
    u64 gh[MAX_HIST]; int ghl=0; gh[ghl++]=hash_pos(&pos);
    char line[4096];

    while (fgets(line, sizeof(line), stdin)) {
        size_t len=strlen(line);
        while (len && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]=0;
        if (!len) continue;

        if (strcmp(line,"uci")==0) {
            printf("id name AlphaBetaNew\nid author team_c\nuciok\n");
            fflush(stdout);
        } else if (strcmp(line,"isready")==0) {
            printf("readyok\n"); fflush(stdout);
        } else if (strcmp(line,"ucinewgame")==0) {
            pos_start(&pos); ghl=0; gh[ghl++]=hash_pos(&pos); tt_clear(); clear_heuristics();
        } else if (strncmp(line,"position",8)==0) {
            parse_position(&pos, line, gh, &ghl);
        } else if (strncmp(line,"go",2)==0) {
            Move ms[320]; int n=legal_moves(&pos,ms);
            if (n<=0) { printf("bestmove 0000\n"); fflush(stdout); }
            else print_bestmove(find_best_move(&pos, gh, ghl, MAX_DEPTH));
        } else if (strcmp(line,"quit")==0) {
            break;
        }
    }
    return 0;
}
#endif /* GTEST_MODE */
