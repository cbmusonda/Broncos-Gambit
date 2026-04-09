#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    int from, to;
    char promo;
} Move;

typedef struct {
    char b[64];
    int white_to_move;
} Pos;

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

    if (by_white) {
        if (r > 0 && f > 0 && p->b[(r - 1) * 8 + (f - 1)] == 'P') return 1;
        if (r > 0 && f < 7 && p->b[(r - 1) * 8 + (f + 1)] == 'P') return 1;
    } else {
        if (r < 7 && f > 0 && p->b[(r + 1) * 8 + (f - 1)] == 'p') return 1;
        if (r < 7 && f < 7 && p->b[(r + 1) * 8 + (f + 1)] == 'p') return 1;
    }

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
{
    int row, column, dir, start_row, promo_row;

    row    = from / 8;
    column = from % 8;

    if (white == 1)
    {
        dir       = 1;
        start_row = 1;
        promo_row = 6;
    }
    else
    {
        dir       = -1;
        start_row = 6;
        promo_row = 1;
    }

    int to = from + dir * 8;
    if (to >= 0 && to < 64 && p->b[to] == '.')
    {
        if (row == promo_row)
        {
            add_move(moves, n, from, to, 'q');
        }
        else
        {
            add_move(moves, n, from, to, 0);
        }

        if (row == start_row)
        {
            int to2 = from + dir * 16;
            if (to2 >= 0 && to2 < 64 && p->b[to2] == '.')
            {
                add_move(moves, n, from, to2, 0);
            }
        }
    }

    int cap_columns[2] = { column - 1, column + 1 };

    for (int i = 0; i < 2; i++)
    {
        int cc = cap_columns[i];
        if (cc < 0 || cc > 7) continue;

        int cap_sq = from + dir * 8 + (cc - column);
        char target = p->b[cap_sq];

        if (target == '.') continue;
        if (is_white_piece(target) == white) continue;

        if (row == promo_row)
        {
            add_move(moves, n, from, cap_sq, 'q');
        }
        else
        {
            add_move(moves, n, from, cap_sq, 0);
        }
    }
}

static void gen_knight(const Pos *p, int from, int white, Move *moves, int *n) {
    static const int nd[8] = {-17, -15, -10, -6, 6, 10, 15, 17};
    static const int size = sizeof(nd)/sizeof(int);
    int to, to_col, to_row, from_col, from_row, col_distance, row_distance;

    int us_white = p->white_to_move;

    from_col = from % 8;
    from_row = from / 8;

    for (size_t i = 0; i < size; i++) {
        to = from + nd[i];

        if (to < 0 || to > 63) continue;

        int white = is_white_piece(p->b[to]);
        if ((p->b[to] != '.') && (white == us_white)) continue;

        to_col = to % 8;
        to_row = to / 8;

        col_distance = abs(from_col - to_col);
        row_distance = abs(from_row - to_row);

        if ((col_distance == 1 && row_distance == 2) ||
            (col_distance == 2 && row_distance == 1)) {
            add_move(moves, n, from, to, 0);
        }
    }
}

static void gen_queen(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int r = from / 8;
    int f = from % 8;

    for (int i = 0; i < dcount; i++) {
        int df = dirs[i][0];
        int dr = dirs[i][1];

        int cr = r + dr;
        int cf = f + df;

        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int to = cr * 8 + cf;
            char target = p->b[to];

            if (target == '.') {
                add_move(moves, n, from, to, 0);
            } else {
                if (is_white_piece(target) != white) {
                    add_move(moves, n, from, to, 0);
                }
                break;
            }
            cr += dr;
            cf += df;
        }
    }
}

static void gen_bishop(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int from_file = from % 8;
    int from_rank = from / 8;

    for (int d = 0; d < dcount; d++) {
        int df = dirs[d][0];
        int dr = dirs[d][1];

        int cf = from_file + df;
        int cr = from_rank + dr;

        while (cf >= 0 && cf < 8 && cr >= 0 && cr < 8) {
            int to = cr * 8 + cf;
            char target = p->b[to];

            if (target == '.') {
                add_move(moves, n, from, to, 0);
            } else {
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

static void gen_rook(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int from_file = from % 8;
    int from_rank = from / 8;

    for (int d = 0; d < dcount; d++) {
        int df = dirs[d][0];
        int dr = dirs[d][1];

        int cf = from_file + df;
        int cr = from_rank + dr;

        while (cf >= 0 && cf < 8 && cr >= 0 && cr < 8) {
            int to = cr * 8 + cf;
            char target = p->b[to];

            if (target == '.') {
                add_move(moves, n, from, to, 0);
            } else {
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
        if (is_square_attacked(p, to, !white)) continue;

        add_move(moves, n, from, to, 0);
    }
}

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

static int evaluate(const Pos *p) {
    int score = 0;
    for (int i = 0; i < 64; i++) {
        char pc = p->b[i];
        int rank = i / 8;
        switch (pc) {
            case 'P': score += 100 + rank;       break;
            case 'N': score += 300 + rank;       break;
            case 'B': score += 300 + rank;       break;
            case 'R': score += 500 + rank;       break;
            case 'Q': score += 900 + rank;       break;
            case 'p': score -= 100 + (7 - rank); break;
            case 'n': score -= 300 + (7 - rank); break;
            case 'b': score -= 300 + (7 - rank); break;
            case 'r': score -= 500 + (7 - rank); break;
            case 'q': score -= 900 + (7 - rank); break;
            default: break;
        }
    }
    return score;
}

#define INF        10000000
#define SEARCH_DEPTH 3
#define MAX_HIST   512

static unsigned int board_key(const Pos *p) {
    unsigned int k = (unsigned int)p->white_to_move;
    for (int i = 0; i < 64; i++)
        k ^= (unsigned int)((i << 8) | (unsigned char)p->b[i]);
    return k;
}

static int minimax(const Pos *p, int depth, int is_max,
                   unsigned int *path_keys, int path_len,
                   unsigned int *game_hist, int game_len) {

    unsigned int key = board_key(p);

    for (int i = 0; i < path_len; i++)
        if (path_keys[i] == key)
            return 0;

    int game_count = 0;
    for (int i = 0; i < game_len; i++)
        if (game_hist[i] == key) game_count++;
    if (game_count >= 2)
        return 0;

    if (depth == 0) return evaluate(p);

    Move moves[256];
    int n = legal_moves(p, moves);

    if (n == 0) {
        if (in_check(p, p->white_to_move))
            return is_max ? -INF : INF;
        return 0;
    }

    if (path_len < MAX_HIST) {
        path_keys[path_len] = key;
        path_len++;
    }

    if (is_max) {
        int best = -INF;
        for (int i = 0; i < n; i++) {
            Pos np = make_move(p, moves[i]);
            int val = minimax(&np, depth - 1, 0, path_keys, path_len, game_hist, game_len);
            if (val > best) best = val;
        }
        return best;
    } else {
        int best = INF;
        for (int i = 0; i < n; i++) {
            Pos np = make_move(p, moves[i]);
            int val = minimax(&np, depth - 1, 1, path_keys, path_len, game_hist, game_len);
            if (val < best) best = val;
        }
        return best;
    }
}

static Move best_move_minimax(const Pos *p, unsigned int *game_hist, int game_len) {
    Move moves[256];
    int n = legal_moves(p, moves);
    Move chosen = moves[0];
    int is_max = p->white_to_move;
    int best_score = is_max ? -INF : INF;

    unsigned int path[MAX_HIST];
    path[0] = board_key(p);
    int path_len = 1;

    for (int i = 0; i < n; i++) {
        Pos np = make_move(p, moves[i]);
        int score = minimax(&np, SEARCH_DEPTH - 1, !is_max, path, path_len, game_hist, game_len);

        unsigned int child_key = board_key(&np);
        int repeat_count = 0;
        for (int j = 0; j < game_len; j++)
            if (game_hist[j] == child_key) repeat_count++;

        if (repeat_count >= 2) {
            score = 0;
        } else if (repeat_count == 1) {
            score = is_max ? (score - 50000) : (score + 50000);
        }

        int better = is_max ? (score > best_score) : (score < best_score);
        if (better) {
            best_score = score;
            chosen = moves[i];
        }
    }
    return chosen;
}

static void print_bestmove(Move m) {
    char a[3], b[3];
    index_to_sq(m.from, a);
    index_to_sq(m.to, b);
    if (m.promo) printf("bestmove %s%s%c\n", a, b, m.promo);
    else printf("bestmove %s%s\n", a, b);
    fflush(stdout);
}

int main(void) {
    Pos pos;
    pos_start(&pos);
    unsigned int game_history[MAX_HIST];
    int game_hist_len = 0;

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
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
            game_hist_len = 0;
        } else if (strncmp(line, "position", 8) == 0) {
            game_hist_len = 0;
            {
                Pos tmp;
                char buf2[1024];
                strncpy(buf2, line, sizeof(buf2)-1);
                buf2[sizeof(buf2)-1] = 0;
                char *save2 = NULL;
                char *toks2[128];
                int nt2 = 0;
                for (char *t = strtok_r(buf2, " \t\r\n", &save2);
                     t && nt2 < 128;
                     t = strtok_r(NULL, " \t\r\n", &save2))
                    toks2[nt2++] = t;

                int ii = 1;
                if (ii < nt2 && strcmp(toks2[ii], "startpos") == 0) {
                    pos_start(&tmp); ii++;
                } else if (ii < nt2 && strcmp(toks2[ii], "fen") == 0) {
                    ii++;
                    char fen2[512] = {0};
                    for (int k = 0; k < 6 && ii < nt2; k++, ii++) {
                        if (k) strcat(fen2, " ");
                        strcat(fen2, toks2[ii]);
                    }
                    pos_from_fen(&tmp, fen2);
                }
                if (game_hist_len < MAX_HIST)
                    game_history[game_hist_len++] = board_key(&tmp);
                if (ii < nt2 && strcmp(toks2[ii], "moves") == 0) {
                    ii++;
                    for (; ii < nt2; ii++) {
                        apply_uci_move(&tmp, toks2[ii]);
                        if (game_hist_len < MAX_HIST)
                            game_history[game_hist_len++] = board_key(&tmp);
                    }
                }
                pos = tmp;
            }
        } else if (strncmp(line, "go", 2) == 0) {
            Move ms[256];
            int n = legal_moves(&pos, ms);
            if (n <= 0) {
                printf("bestmove 0000\n");
                fflush(stdout);
            } else {
                Move best = best_move_minimax(&pos, game_history, game_hist_len);
                Pos after = make_move(&pos, best);
                if (game_hist_len < MAX_HIST)
                    game_history[game_hist_len++] = board_key(&after);
                print_bestmove(best);
            }
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }
    return 0;
}