#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned long long u64;
typedef struct { int from, to; char promo; } Move;
typedef struct { char b[64]; int white_to_move; } Pos;

/* ── Zobrist ── */
static u64 ZP[64][12], ZS;
static int zr=0;
static int piece_index(char c){switch(c){case 'P':return 0;case 'N':return 1;case 'B':return 2;case 'R':return 3;case 'Q':return 4;case 'K':return 5;case 'p':return 6;case 'n':return 7;case 'b':return 8;case 'r':return 9;case 'q':return 10;case 'k':return 11;default:return -1;}}
static u64 lcg(u64*s){*s=*s*6364136223846793005ULL+1442695040888963407ULL;return *s^(*s>>33);}
static void init_zobrist(void){if(zr)return;u64 s=0xdeadbeefcafeULL;for(int i=0;i<64;i++)for(int j=0;j<12;j++)ZP[i][j]=lcg(&s);ZS=lcg(&s);zr=1;}
static u64 hash_pos(const Pos*p){u64 h=0;for(int i=0;i<64;i++){int pi=piece_index(p->b[i]);if(pi>=0)h^=ZP[i][pi];}if(!p->white_to_move)h^=ZS;return h;}

/* ── Board ── */
static int sq_index(const char*s){return(s[1]-'1')*8+(s[0]-'a');}
static void index_to_sq(int idx,char out[3]){out[0]='a'+idx%8;out[1]='1'+idx/8;out[2]=0;}
static int is_white_piece(char c){return c>='A'&&c<='Z';}
static void pos_from_fen(Pos*p,const char*fen){
    memset(p->b,'.',64);p->white_to_move=1;
    char buf[256];strncpy(buf,fen,255);buf[255]=0;
    char*sv=NULL,*pl=strtok_r(buf," ",&sv),*stm=strtok_r(NULL," ",&sv);
    if(stm)p->white_to_move=(strcmp(stm,"w")==0);
    int rank=7,file=0;
    for(size_t i=0;pl&&pl[i];i++){char c=pl[i];if(c=='/'){rank--;file=0;}else if(isdigit((unsigned char)c))file+=c-'0';else{int idx=rank*8+file;if(idx>=0&&idx<64)p->b[idx]=c;file++;}}
}
static void pos_start(Pos*p){pos_from_fen(p,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1");}

/* ── Attack / check ── */
static int is_square_attacked(const Pos*p,int sq,int by_white){
    int r=sq/8,f=sq%8;
    if(by_white){if(r>0&&f>0&&p->b[(r-1)*8+(f-1)]=='P')return 1;if(r>0&&f<7&&p->b[(r-1)*8+(f+1)]=='P')return 1;}
    else{if(r<7&&f>0&&p->b[(r+1)*8+(f-1)]=='p')return 1;if(r<7&&f<7&&p->b[(r+1)*8+(f+1)]=='p')return 1;}
    static const int nd[8]={-17,-15,-10,-6,6,10,15,17};
    for(int i=0;i<8;i++){int to=sq+nd[i];if(to<0||to>=64)continue;int dr=abs(to/8-r),df=abs(to%8-f);if(!((dr==1&&df==2)||(dr==2&&df==1)))continue;char pc=p->b[to];if(by_white&&pc=='N')return 1;if(!by_white&&pc=='n')return 1;}
    static const int dirs[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for(int di=0;di<8;di++){int df2=dirs[di][0],dr2=dirs[di][1],cr=r+dr2,cf=f+df2;while(cr>=0&&cr<8&&cf>=0&&cf<8){int idx=cr*8+cf;char pc=p->b[idx];if(pc!='.'){if(is_white_piece(pc)==by_white){char up=(char)toupper((unsigned char)pc);if(up=='Q')return 1;if(di<4&&up=='R')return 1;if(di>=4&&up=='B')return 1;if(up=='K'&&abs(cr-r)<=1&&abs(cf-f)<=1)return 1;}break;}cr+=dr2;cf+=df2;}}
    for(int rr=r-1;rr<=r+1;rr++)for(int ff=f-1;ff<=f+1;ff++){if(rr<0||rr>=8||ff<0||ff>=8||(rr==r&&ff==f))continue;char pc=p->b[rr*8+ff];if(by_white&&pc=='K')return 1;if(!by_white&&pc=='k')return 1;}
    return 0;
}
static int in_check(const Pos*p,int wk){char k=wk?'K':'k';for(int i=0;i<64;i++)if(p->b[i]==k)return is_square_attacked(p,i,!wk);return 1;}

/* ── Make move ── */
static Pos make_move(const Pos*p,Move m){
    Pos np=*p;char pc=np.b[m.from];np.b[m.from]='.';
    char placed=pc;if(m.promo&&(pc=='P'||pc=='p'))placed=is_white_piece(pc)?(char)toupper((unsigned char)m.promo):(char)tolower((unsigned char)m.promo);
    np.b[m.to]=placed;
    if(pc=='K'&&m.from==4){if(m.to==6){np.b[7]='.';np.b[5]='R';}else if(m.to==2){np.b[0]='.';np.b[3]='R';}}
    if(pc=='k'&&m.from==60){if(m.to==62){np.b[63]='.';np.b[61]='r';}else if(m.to==58){np.b[56]='.';np.b[59]='r';}}
    np.white_to_move=!p->white_to_move;return np;
}
static void add_move(Move*mv,int*n,int f,int t,char pr){mv[*n].from=f;mv[*n].to=t;mv[*n].promo=pr;(*n)++;}

/* ── Move generators ── */
static void gen_pawn(const Pos*p,int from,int white,Move*mv,int*n){
    int row=from/8,col=from%8,dir,sr,pr2;
    if(white){dir=1;sr=1;pr2=6;}else{dir=-1;sr=6;pr2=1;}
    int to=from+dir*8;
    if(to>=0&&to<64&&p->b[to]=='.'){
        if(row==pr2){char ps[4]={'q','r','b','n'};for(int i=0;i<4;i++)add_move(mv,n,from,to,ps[i]);}
        else{add_move(mv,n,from,to,0);if(row==sr){int t2=from+dir*16;if(t2>=0&&t2<64&&p->b[t2]=='.')add_move(mv,n,from,t2,0);}}
    }
    int caps[2]={col-1,col+1};
    for(int i=0;i<2;i++){int cc=caps[i];if(cc<0||cc>7)continue;int csq=from+dir*8+(cc-col);if(csq<0||csq>=64)continue;char tgt=p->b[csq];if(tgt=='.'||is_white_piece(tgt)==white)continue;if(row==pr2){char ps[4]={'q','r','b','n'};for(int j=0;j<4;j++)add_move(mv,n,from,csq,ps[j]);}else add_move(mv,n,from,csq,0);}
}
static void gen_knight(const Pos*p,int from,int white,Move*mv,int*n){
    static const int nd[8]={-17,-15,-10,-6,6,10,15,17};int fc=from%8,fr=from/8;
    for(int i=0;i<8;i++){int to=from+nd[i];if(to<0||to>63)continue;if(!((abs(fc-to%8)==1&&abs(fr-to/8)==2)||(abs(fc-to%8)==2&&abs(fr-to/8)==1)))continue;char tgt=p->b[to];if(tgt!='.'&&is_white_piece(tgt)==white)continue;add_move(mv,n,from,to,0);}
}
static void gen_slider(const Pos*p,int from,int white,const int dirs[][2],int dc,Move*mv,int*n){
    int r=from/8,f=from%8;for(int d=0;d<dc;d++){int cr=r+dirs[d][1],cf=f+dirs[d][0];while(cr>=0&&cr<8&&cf>=0&&cf<8){int to=cr*8+cf;char tgt=p->b[to];if(tgt=='.'){add_move(mv,n,from,to,0);}else{if(is_white_piece(tgt)!=white)add_move(mv,n,from,to,0);break;}cr+=dirs[d][1];cf+=dirs[d][0];}}
}
static void gen_bishop(const Pos*p,int from,int white,const int d[][2],int dc,Move*mv,int*n){gen_slider(p,from,white,d,dc,mv,n);}
static void gen_rook  (const Pos*p,int from,int white,const int d[][2],int dc,Move*mv,int*n){gen_slider(p,from,white,d,dc,mv,n);}
static void gen_queen (const Pos*p,int from,int white,const int d[][2],int dc,Move*mv,int*n){gen_slider(p,from,white,d,dc,mv,n);}
static void gen_king(const Pos*p,int from,int white,Move*mv,int*n){
    static const int dirs[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    int fr=from/8,fc=from%8;
    for(int i=0;i<8;i++){int tr=fr+dirs[i][1],tc=fc+dirs[i][0];if(tr<0||tr>7||tc<0||tc>7)continue;int to=tr*8+tc;char tgt=p->b[to];if(tgt!='.'&&is_white_piece(tgt)==white)continue;add_move(mv,n,from,to,0);}
    if(white&&from==4){if(p->b[5]=='.'&&p->b[6]=='.'&&p->b[7]=='R')add_move(mv,n,4,6,0);if(p->b[1]=='.'&&p->b[2]=='.'&&p->b[3]=='.'&&p->b[0]=='R')add_move(mv,n,4,2,0);}
    if(!white&&from==60){if(p->b[61]=='.'&&p->b[62]=='.'&&p->b[63]=='r')add_move(mv,n,60,62,0);if(p->b[57]=='.'&&p->b[58]=='.'&&p->b[59]=='.'&&p->b[56]=='r')add_move(mv,n,60,58,0);}
}
static int pseudo_legal_moves(const Pos*p,Move*mv){
    int n=0;for(int i=0;i<64;i++){char pc=p->b[i];if(pc=='.')continue;int white=is_white_piece(pc);if(white!=p->white_to_move)continue;char up=(char)toupper((unsigned char)pc);
    if(up=='P')gen_pawn(p,i,white,mv,&n);else if(up=='N')gen_knight(p,i,white,mv,&n);
    else if(up=='B'){static const int d[4][2]={{1,1},{1,-1},{-1,1},{-1,-1}};gen_bishop(p,i,white,d,4,mv,&n);}
    else if(up=='R'){static const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};gen_rook(p,i,white,d,4,mv,&n);}
    else if(up=='Q'){static const int d[8][2]={{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};gen_queen(p,i,white,d,8,mv,&n);}
    else if(up=='K')gen_king(p,i,white,mv,&n);}return n;
}
static int legal_moves(const Pos*p,Move*out){Move tmp[320];int pn=pseudo_legal_moves(p,tmp),n=0;for(int i=0;i<pn;i++){Pos np=make_move(p,tmp[i]);if(!in_check(&np,!np.white_to_move))out[n++]=tmp[i];}return n;}
static void apply_uci_move(Pos*p,const char*uci){if(!uci||strlen(uci)<4)return;Move m;m.from=sq_index(uci);m.to=sq_index(uci+2);m.promo=strlen(uci)>=5?uci[4]:0;*p=make_move(p,m);}

/* ── Evaluation ── */
static const int PIECE_VAL[26]={['P'-'A']=100,['N'-'A']=320,['B'-'A']=330,['R'-'A']=500,['Q'-'A']=900,['K'-'A']=20000};
static const int PST_PAWN[64]={0,0,0,0,0,0,0,0,50,50,50,50,50,50,50,50,10,10,20,30,30,20,10,10,5,5,10,25,25,10,5,5,0,0,0,20,20,0,0,0,5,-5,-10,0,0,-10,-5,5,5,10,10,-20,-20,10,10,5,0,0,0,0,0,0,0,0};
static const int PST_KNIGHT[64]={-50,-40,-30,-30,-30,-30,-40,-50,-40,-20,0,0,0,0,-20,-40,-30,0,10,15,15,10,0,-30,-30,5,15,20,20,15,5,-30,-30,0,15,20,20,15,0,-30,-30,5,10,15,15,10,5,-30,-40,-20,0,5,5,0,-20,-40,-50,-40,-30,-30,-30,-30,-40,-50};
static const int PST_BISHOP[64]={-20,-10,-10,-10,-10,-10,-10,-20,-10,0,0,0,0,0,0,-10,-10,0,5,10,10,5,0,-10,-10,5,5,10,10,5,5,-10,-10,0,10,10,10,10,0,-10,-10,10,10,10,10,10,10,-10,-10,5,0,0,0,0,5,-10,-20,-10,-10,-10,-10,-10,-10,-20};
static const int PST_ROOK[64]={0,0,0,0,0,0,0,0,5,10,10,10,10,10,10,5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,-5,0,0,0,0,0,0,-5,0,0,0,5,5,0,0,0};
static const int PST_QUEEN[64]={-20,-10,-10,-5,-5,-10,-10,-20,-10,0,0,0,0,0,0,-10,-10,0,5,5,5,5,0,-10,-5,0,5,5,5,5,0,-5,0,0,5,5,5,5,0,-5,-10,5,5,5,5,5,0,-10,-10,0,5,0,0,0,0,-10,-20,-10,-10,-5,-5,-10,-10,-20};
static const int PST_KING[64]={-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-30,-40,-40,-50,-50,-40,-40,-30,-20,-30,-30,-40,-40,-30,-30,-20,-10,-20,-20,-20,-20,-20,-20,-10,20,20,0,0,0,0,20,20,20,30,10,0,0,10,30,20};
static int pst_lookup(char up,int sq){switch(up){case 'P':return PST_PAWN[sq];case 'N':return PST_KNIGHT[sq];case 'B':return PST_BISHOP[sq];case 'R':return PST_ROOK[sq];case 'Q':return PST_QUEEN[sq];case 'K':return PST_KING[sq];default:return 0;}}
static int evaluate(const Pos*p){int score=0;for(int i=0;i<64;i++){char pc=p->b[i];if(pc=='.')continue;int white=is_white_piece(pc);char up=(char)toupper((unsigned char)pc);int idx=up-'A';if(idx<0||idx>=26)continue;int sq=white?i:(7-i/8)*8+(i%8);int val=PIECE_VAL[idx]+pst_lookup(up,sq);score+=white?val:-val;}return score;}

/* ── Move ordering ── */
static int move_score(const Pos*p,Move m){char victim=p->b[m.to],attacker=p->b[m.from];if(victim!='.'){int vi=toupper((unsigned char)victim)-'A',ai=toupper((unsigned char)attacker)-'A';int vv=(vi>=0&&vi<26)?PIECE_VAL[vi]:0,av=(ai>=0&&ai<26)?PIECE_VAL[ai]:0;return 10000+vv-av/10;}if(m.promo)return 9000;return 0;}
static void order_moves(const Pos*p,Move*ms,int n){for(int i=1;i<n;i++){Move tm=ms[i];int ts=move_score(p,tm),j=i-1;while(j>=0&&move_score(p,ms[j])<ts){ms[j+1]=ms[j];j--;}ms[j+1]=tm;}}

/* ── Search ── */
#define INF      1000000
#define MAX_HIST 1024

/*
 * CONTEMPT: how much we dislike a draw relative to the current position.
 * Positive = we prefer to play on rather than draw (aggressive).
 * This makes repetition-draws WORSE than a flat 0, so the engine
 * actively avoids repeating positions even when material is equal.
 */
#define CONTEMPT 200

static int nodes_searched;

static int count_reps(const u64*hist,int len,u64 h){int c=0;for(int i=0;i<len;i++)if(hist[i]==h)c++;return c;}

static int alpha_beta(const Pos*p,int depth,int alpha,int beta,u64*hist,int hlen){
    nodes_searched++;
    u64 h=hash_pos(p);
    int reps=count_reps(hist,hlen,h);

    /*
     * Repetition scoring:
     *   1st repeat (reps==1): score as slight disadvantage using contempt.
     *     This makes the engine prefer a fresh position over returning here.
     *   2nd repeat (reps>=2): score as draw (0). Avoids threefold infinite loop.
     *
     * From white's POV: white wants high scores, so a draw (-CONTEMPT from
     * white's perspective when white repeats) is bad for white.
     * From black's POV: same logic flipped.
     * We use: draw_score = p->white_to_move ? -CONTEMPT : CONTEMPT
     * This means whichever side is about to repeat pays the contempt penalty.
     */
    if(reps>=2) return p->white_to_move ? -CONTEMPT : CONTEMPT;
    if(reps==1) return p->white_to_move ? -CONTEMPT*2 : CONTEMPT*2;

    Move ms[320];int n=legal_moves(p,ms);
    if(n==0){return in_check(p,p->white_to_move)?(p->white_to_move?-INF+hlen:INF-hlen):0;}
    if(depth==0)return evaluate(p);

    order_moves(p,ms,n);
    if(hlen<MAX_HIST)hist[hlen]=h;

    if(p->white_to_move){
        int best=-INF;
        for(int i=0;i<n;i++){Pos np=make_move(p,ms[i]);int score=alpha_beta(&np,depth-1,alpha,beta,hist,hlen+1);if(score>best)best=score;if(score>alpha)alpha=score;if(alpha>=beta)break;}
        return best;
    }else{
        int best=INF;
        for(int i=0;i<n;i++){Pos np=make_move(p,ms[i]);int score=alpha_beta(&np,depth-1,alpha,beta,hist,hlen+1);if(score<best)best=score;if(score<beta)beta=score;if(alpha>=beta)break;}
        return best;
    }
}

static Move find_best_move(const Pos*p,const u64*gh,int ghl,int max_depth){
    Move ms[320];int n=legal_moves(p,ms);
    if(n==0){Move m={0,0,0};return m;}
    order_moves(p,ms,n);
    Move best=ms[0];
    u64 sh[MAX_HIST];int shl=ghl<MAX_HIST?ghl:MAX_HIST-1;
    for(int i=0;i<shl;i++)sh[i]=gh[i];

    /*
     * If the current root position has already appeared >= 2 times in game history,
     * we are about to create a threefold repetition. Force the engine to pick a
     * non-repeating move (one whose resulting position hash is not already in gh[]).
     * Only fall back to a repeating move if ALL moves repeat (total zugzwang corner).
     */
    u64 root_hash=hash_pos(p);
    int root_reps=count_reps(gh,ghl,root_hash);
    if(root_reps>=2){
        /* Filter: prefer moves whose child position is NOT in gh[] */
        Move non_rep[320];int nr=0;
        for(int i=0;i<n;i++){
            Pos np=make_move(p,ms[i]);
            u64 nh=hash_pos(&np);
            if(count_reps(gh,ghl,nh)==0) non_rep[nr++]=ms[i];
        }
        if(nr>0){
            /* Search only among non-repeating moves */
            Move bd=non_rep[0];int bs=p->white_to_move?-INF:INF;
            for(int depth=1;depth<=max_depth;depth++){
                bd=non_rep[0];bs=p->white_to_move?-INF:INF;nodes_searched=0;
                for(int i=0;i<nr;i++){Pos np=make_move(p,non_rep[i]);int score=alpha_beta(&np,depth-1,-INF,INF,sh,shl);if(p->white_to_move?(score>bs):(score<bs)){bs=score;bd=non_rep[i];}}
            }
            return bd;
        }
    }

    for(int depth=1;depth<=max_depth;depth++){
        Move bd=ms[0];int bs=p->white_to_move?-INF:INF;nodes_searched=0;
        for(int i=0;i<n;i++){Pos np=make_move(p,ms[i]);int score=alpha_beta(&np,depth-1,-INF,INF,sh,shl);if(p->white_to_move?(score>bs):(score<bs)){bs=score;bd=ms[i];}}
        best=bd;
    }
    return best;
}

/* ── Position parsing ── */
static void parse_position(Pos*p,const char*line,u64*gh,int*ghl){
    char buf[4096];strncpy(buf,line,4095);buf[4095]=0;
    char*toks[512];int nt=0;char*sv=NULL;
    for(char*tok=strtok_r(buf," \t\r\n",&sv);tok&&nt<512;tok=strtok_r(NULL," \t\r\n",&sv))toks[nt++]=tok;
    int i=1;
    if(i<nt&&strcmp(toks[i],"startpos")==0){pos_start(p);i++;}
    else if(i<nt&&strcmp(toks[i],"fen")==0){i++;char fen[512]={0};for(int k=0;k<6&&i<nt;k++,i++){if(k)strcat(fen," ");strcat(fen,toks[i]);}pos_from_fen(p,fen);}
    *ghl=0;gh[(*ghl)++]=hash_pos(p);
    if(i<nt&&strcmp(toks[i],"moves")==0){i++;for(;i<nt;i++){apply_uci_move(p,toks[i]);if(*ghl<MAX_HIST)gh[(*ghl)++]=hash_pos(p);}}
}

static void print_bestmove(Move m){char a[3],b[3];index_to_sq(m.from,a);index_to_sq(m.to,b);if(m.promo)printf("bestmove %s%s%c\n",a,b,m.promo);else printf("bestmove %s%s\n",a,b);fflush(stdout);}

/* ── Main ── */
#define MAX_DEPTH 4

#ifndef GTEST_MODE
int main(void){
    init_zobrist();Pos pos;pos_start(&pos);
    u64 gh[MAX_HIST];int ghl=0;gh[ghl++]=hash_pos(&pos);
    char line[4096];
    while(fgets(line,sizeof(line),stdin)){
        size_t len=strlen(line);while(len&&(line[len-1]=='\n'||line[len-1]=='\r'))line[--len]=0;if(!len)continue;
        if(strcmp(line,"uci")==0){printf("id name team_c\nid author team_c_bryan\nuciok\n");fflush(stdout);}
        else if(strcmp(line,"isready")==0){printf("readyok\n");fflush(stdout);}
        else if(strcmp(line,"ucinewgame")==0){pos_start(&pos);ghl=0;gh[ghl++]=hash_pos(&pos);}
        else if(strncmp(line,"position",8)==0){parse_position(&pos,line,gh,&ghl);}
        else if(strncmp(line,"go",2)==0){Move ms[320];int n=legal_moves(&pos,ms);if(n<=0){printf("bestmove 0000\n");fflush(stdout);}else{print_bestmove(find_best_move(&pos,gh,ghl,MAX_DEPTH));}}
        else if(strcmp(line,"quit")==0){break;}
    }
    return 0;
}
#endif // GTEST_MODE