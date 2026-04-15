/*
 * shell.c — GameRank DB 대화형 쉘
 *
 * 레코드 삽입 시 ID 자동 부여 후 B+ 트리 인덱스에 등록.
 * ID 기반 탐색은 B+ 트리를 사용하고,
 * 다른 필드(win_rate 등) 기반 탐색은 선형 탐색을 수행한다.
 *
 * 사용법: ./bin/shell [csv_path]
 *
 * 지원 명령:
 *   INSERT <name> <win_rate> [rank]   레코드 삽입 (ID 자동 부여)
 *   SELECT ID <n>           B+ 트리로 ID 탐색
 *   SELECT NAME <name>      선형 탐색으로 nickname 탐색
 *   SELECT WINRATE <n>      선형 탐색으로 win_rate >= n 인 레코드 수 조회
 *   LOAD <n>                n개 레코드 랜덤 삽입
 *   LOADCSV [path]          CSV 파일 로드 (기본: data/players_1000000.csv)
 *   LIST [n]                처음 n개 레코드 출력 (기본 10)
 *   COUNT                   현재 레코드 총 수 출력
 *   BENCH                   현재 테이블로 단일/범위 탐색 벤치마크
 *   HELP                    명령어 목록 출력
 *   QUIT                    종료
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#include "linear/linear_search.h"
#include "btree/btree.h"
#include "bplus_tree/bplus_tree.h"

/* ── 전역 연산 횟수 카운터 (benchmark.c와 동일 방식) ── */
long long g_op_count = 0;

/* ── 상수 ─────────────────────────────────────────── */

#define INIT_CAPACITY 1024   /* 초기 테이블 용량 */
#define BENCH_ITERS   5      /* 벤치마크 반복 횟수 */
#define DEFAULT_CSV_PATH "data/players_1000000.csv"

static const char *RAND_NAMES[] = {
    "ShadowBlade","NightFury","CrimsonAce","StarlightX","IronWolf",
    "PixelKing","VoidHunter","FrostByte","ThunderX","NeonRift",
    "DarkMatter","CyberGhost","RedStorm","BlueFang","GoldRush",
    "SilverEdge","PurpleRain","OmegaZero","AlphaStrike","BetaForce"
};
#define RAND_NAME_COUNT 20

/* ── 테이블 구조체 ─────────────────────────────────── */

typedef struct {
    Player *records;    /* 레코드 동적 배열 */
    int     count;      /* 현재 레코드 수 */
    int     capacity;   /* 배열 최대 용량 */
    int     next_id;    /* 다음 자동 부여 ID */
    BPTree *index;      /* B+ 트리 인덱스 (id 기준) */
} Table;

/* ── 헬퍼 ─────────────────────────────────────────── */

static long long now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static const char *win_rate_to_rank(double win_rate) {
    if (win_rate >= 85.0) return "챌린저";
    if (win_rate >= 75.0) return "다이아";
    if (win_rate >= 65.0) return "플래티넘";
    if (win_rate >= 55.0) return "골드";
    return "실버";
}

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ── 테이블 API ────────────────────────────────────── */

/* 테이블 생성 */
static Table *table_create(void) {
    Table *t    = malloc(sizeof(Table));
    t->records  = malloc(sizeof(Player) * INIT_CAPACITY);
    t->count    = 0;
    t->capacity = INIT_CAPACITY;
    t->next_id  = 1;
    t->index    = bptree_create();
    return t;
}

/* 인덱스 재구축 */
static void table_rebuild_index(Table *t) {
    bptree_free(t->index);
    t->index = bptree_create();
    for (int i = 0; i < t->count; i++) {
        bptree_insert(t->index, t->records[i].id, &t->records[i]);
    }
}

/* 최소 용량 확보 */
static int table_ensure_capacity(Table *t, int needed) {
    if (needed <= t->capacity) return 1;

    int new_cap = t->capacity;
    while (new_cap < needed) new_cap *= 2;

    Player *old_records = t->records;
    Player *resized = realloc(t->records, sizeof(Player) * new_cap);
    if (!resized) {
        fprintf(stderr, "메모리 부족\n");
        return 0;
    }

    t->records = resized;
    t->capacity = new_cap;

    if (resized != old_records && t->count > 0) {
        table_rebuild_index(t);
    }
    return 1;
}

/* 테이블 비우기 */
static void table_clear(Table *t) {
    t->count = 0;
    t->next_id = 1;
    bptree_free(t->index);
    t->index = bptree_create();
}

/* CSV/외부 데이터용 레코드 삽입 */
static Player *table_insert_loaded(Table *t, int id, const char *name, double win_rate, const char *rank) {
    if (!table_ensure_capacity(t, t->count + 1)) return NULL;

    Player *p = &t->records[t->count];
    p->id = id;
    strncpy(p->name, name, 31);
    p->name[31] = '\0';
    p->win_rate = win_rate;
    strncpy(p->rank, rank && rank[0] ? rank : win_rate_to_rank(win_rate), sizeof(p->rank) - 1);
    p->rank[sizeof(p->rank) - 1] = '\0';

    bptree_insert(t->index, p->id, p);
    t->count++;
    if (id >= t->next_id) t->next_id = id + 1;
    return p;
}

/* 레코드 삽입: ID 자동 부여 후 B+ 트리 인덱스 등록 */
static Player *table_insert(Table *t, const char *name, double win_rate, const char *rank) {
    int id = t->next_id;
    Player *p = table_insert_loaded(t, id, name, win_rate, rank && rank[0] ? rank : win_rate_to_rank(win_rate));
    if (p) t->next_id = id + 1;
    return p;
}

/* ── 명령 구현 ─────────────────────────────────────── */

/* INSERT <name> <win_rate> [rank] */
static void cmd_insert(Table *t, const char *name, double win_rate, const char *rank) {
    Player *p = table_insert(t, name, win_rate, rank);
    if (p) {
        printf("  INSERT OK — ID=%d  nickname=%s  win_rate=%.2f%%  rank=%s\n",
               p->id, p->name, p->win_rate, p->rank);
    }
}

/* SELECT ID <n> — B+ 트리 인덱스 탐색 */
static void cmd_select_id(Table *t, int id) {
    long long t0 = now_us();
    Player *p = (Player *)bptree_search(t->index, id);
    long long elapsed = now_us() - t0;

    if (p) {
        printf("  [B+ 트리] ID=%d  nickname=%s  win_rate=%.2f%%  rank=%s  (%lld μs)\n",
               p->id, p->name, p->win_rate, p->rank, elapsed);
    } else {
        printf("  [B+ 트리] ID=%d — 없음  (%lld μs)\n", id, elapsed);
    }
}

/* SELECT NAME <name> — 선형 탐색 */
static void cmd_select_name(Table *t, const char *name) {
    long long t0 = now_us();
    Player *p = linear_search_name(t->records, t->count, name);
    long long elapsed = now_us() - t0;

    if (p) {
        printf("  [선형 탐색] nickname=%s  -> ID=%d  win_rate=%.2f%%  rank=%s  (%lld μs)\n",
               p->name, p->id, p->win_rate, p->rank, elapsed);
    } else {
        printf("  [선형 탐색] nickname=%s — 없음  (%lld μs)\n", name, elapsed);
    }
}

/* SELECT WINRATE <n> — 선형 탐색 (win_rate >= n) */
static void cmd_select_win_rate(Table *t, double min_win_rate) {
    long long t0 = now_us();
    int count = 0;
    for (int i = 0; i < t->count; i++) {
        if (t->records[i].win_rate >= min_win_rate) count++;
    }
    long long elapsed = now_us() - t0;
    printf("  [선형 탐색] win_rate >= %.2f%% 인 레코드: %d건  (%lld μs)\n",
           min_win_rate, count, elapsed);
}

/* LOAD <n> — n개 랜덤 레코드 일괄 삽입 */
static void cmd_load(Table *t, int n) {
    printf("  %d개 레코드 삽입 중...\n", n);
    long long t0 = now_us();
    for (int i = 0; i < n; i++) {
        char name[32];
        double win_rate;
        snprintf(name, sizeof(name), "%s_%d",
                 RAND_NAMES[rand() % RAND_NAME_COUNT], i % 9999);
        win_rate = 42.0 + (rand() % 5700) / 100.0;
        table_insert(t, name, win_rate, NULL);
    }
    long long elapsed = now_us() - t0;
    printf("  LOAD 완료 — %d건 삽입 (총 %d건)  %lld ms\n",
           n, t->count, elapsed / 1000);
}

/* CSV 레코드 수 미리 계산 */
static int csv_count_rows(FILE *fp) {
    char line[256];
    int rows = 0;

    if (!fgets(line, sizeof(line), fp)) {
        rewind(fp);
        return 0;
    }

    if (!starts_with(line, "id,")) rows++;
    while (fgets(line, sizeof(line), fp)) rows++;

    rewind(fp);
    return rows;
}

/* LOADCSV [path] — CSV 파일을 읽어 테이블/인덱스 구성 */
static void cmd_load_csv(Table *t, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("  CSV 열기 실패: %s (%s)\n", path, strerror(errno));
        return;
    }

    int rows = csv_count_rows(fp);
    if (!table_ensure_capacity(t, rows > 0 ? rows : INIT_CAPACITY)) {
        fclose(fp);
        return;
    }

    table_clear(t);

    printf("  CSV 로드 중... %s\n", path);
    long long t0 = now_us();

    char line[256];
    int line_no = 0;
    int loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        int id;
        char name[32];
        double win_rate;
        char rank[24];

        line_no++;
        if (line_no == 1 && starts_with(line, "id,")) continue;

        if (sscanf(line, "%d,%31[^,],%lf,%23[^,\r\n]", &id, name, &win_rate, rank) != 4) {
            printf("  CSV 파싱 실패: %d번째 줄\n", line_no);
            table_clear(t);
            fclose(fp);
            return;
        }

        if (!table_insert_loaded(t, id, name, win_rate, rank)) {
            fclose(fp);
            return;
        }
        loaded++;
    }

    fclose(fp);
    printf("  LOADCSV 완료 — %d건 로드 (총 %d건)  %lld ms\n",
           loaded, t->count, (now_us() - t0) / 1000);
}

/* LIST [n] — 처음 n개 레코드 출력 */
static void cmd_list(Table *t, int limit) {
    if (t->count == 0) { printf("  레코드 없음\n"); return; }
    if (limit <= 0 || limit > t->count) limit = t->count;
    printf("  %-8s %-24s %-10s %s\n", "ID", "Nickname", "Win Rate", "Rank");
    printf("  %-8s %-24s %-10s %s\n", "--------", "------------------------", "----------", "----------");
    for (int i = 0; i < limit; i++) {
        Player *p = &t->records[i];
        char win_rate_text[16];
        snprintf(win_rate_text, sizeof(win_rate_text), "%.2f%%", p->win_rate);
        printf("  %-8d %-24s %-10s %s\n", p->id, p->name, win_rate_text, p->rank);
    }
}

/* COUNT — 현재 레코드 수 */
static void cmd_count(Table *t) {
    printf("  총 레코드 수: %d\n", t->count);
}

/* BENCH — 현재 테이블로 단일/범위 탐색 벤치마크 */
static void cmd_bench(Table *t) {
    if (t->count < 2) {
        printf("  레코드가 너무 적습니다. 먼저 LOAD 또는 LOADCSV로 데이터를 준비하세요.\n");
        return;
    }
    int target = t->records[t->count / 2].id;   /* 중간값 ID */
    const char *target_name = t->records[t->count / 2].name;
    Player *resolved = linear_search_name(t->records, t->count, target_name);
    int resolved_id = resolved ? resolved->id : 0;
    int lo     = 1;
    int hi     = t->records[(int)(t->count * 0.01)].id;  /* 상위 1% */

    printf("  벤치마크 (n=%d, 단일=ID#%d, 범위=%d~%d, %d회 평균)\n",
           t->count, target, lo, hi, BENCH_ITERS);

    long long ls=0, bs=0, bps=0, lns=0, bns=0, bpns=0, lr=0, br=0, bpr=0;
    long long t0, t1;

    /* B 트리는 shell의 Table 구조에 포함되지 않으므로 별도 생성 */
    BTree *bt = btree_create();
    for (int i = 0; i < t->count; i++)
        btree_insert(bt, t->records[i].id, &t->records[i]);

    for (int i = 0; i < BENCH_ITERS; i++) {
        t0=now_us(); linear_search(t->records, t->count, target); t1=now_us(); ls+=t1-t0;
        t0=now_us(); btree_search(bt, target);                    t1=now_us(); bs+=t1-t0;
        t0=now_us(); bptree_search(t->index, target);             t1=now_us(); bps+=t1-t0;
        t0=now_us(); linear_search_name(t->records, t->count, target_name); t1=now_us(); lns+=t1-t0;
        t0=now_us(); btree_search(bt, resolved_id);                           t1=now_us(); bns+=t1-t0;
        t0=now_us(); bptree_search(t->index, resolved_id);                    t1=now_us(); bpns+=t1-t0;
        t0=now_us(); linear_range(t->records, t->count, lo, hi);  t1=now_us(); lr+=t1-t0;
        t0=now_us(); btree_range(bt, lo, hi);                     t1=now_us(); br+=t1-t0;
        t0=now_us(); bptree_range(t->index, lo, hi);              t1=now_us(); bpr+=t1-t0;
    }
    printf("\n  %-12s %12s %12s\n", "", "단일 탐색", "범위 탐색");
    printf("  %-12s %12lld μs %12lld μs\n", "선형 탐색", ls/BENCH_ITERS,  lr/BENCH_ITERS);
    printf("  %-12s %12lld μs %12lld μs\n", "B 트리",    bs/BENCH_ITERS,  br/BENCH_ITERS);
    printf("  %-12s %12lld μs %12lld μs\n", "B+ 트리",   bps/BENCH_ITERS, bpr/BENCH_ITERS);
    printf("\n  이름 기준 브리지 비교 (nickname=%s -> resolved ID=%d)\n", target_name, resolved_id);
    printf("  %-12s %12s\n", "", "단일 탐색");
    printf("  %-12s %12lld μs\n", "선형(name)", lns/BENCH_ITERS);
    printf("  %-12s %12lld μs\n", "B 트리(id)", bns/BENCH_ITERS);
    printf("  %-12s %12lld μs\n", "B+ 트리(id)", bpns/BENCH_ITERS);
    printf("  * name은 선형 탐색으로 먼저 찾고, 찾은 레코드의 ID로 트리를 다시 조회합니다.\n");
    btree_free(bt);
}

/* HELP */
static void cmd_help(void) {
    printf("  명령어 목록:\n");
    printf("    INSERT <name> <win_rate> [rank]   레코드 삽입 (ID 자동 부여)\n");
    printf("    SELECT ID <n>           B+ 트리로 ID 탐색\n");
    printf("    SELECT NAME <name>      선형 탐색으로 nickname 탐색\n");
    printf("    SELECT WINRATE <n>      선형 탐색 (win_rate >= n)\n");
    printf("    LOAD <n>                n개 랜덤 레코드 일괄 삽입\n");
    printf("    LOADCSV [path]          CSV 파일 로드 (기본: %s)\n", DEFAULT_CSV_PATH);
    printf("    LIST [n]                처음 n개 레코드 출력 (기본 10)\n");
    printf("    COUNT                   현재 레코드 총 수\n");
    printf("    BENCH                   현재 테이블 벤치마크\n");
    printf("    HELP                    이 도움말\n");
    printf("    QUIT                    종료\n");
}

/* ── main ──────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    srand((unsigned)time(NULL));
    Table *t = table_create();

    printf("=== GameRank DB Shell ===\n");
    printf("HELP 로 명령어 확인\n\n");

    if (argc >= 2) {
        cmd_load_csv(t, argv[1]);
    }

    char line[256];
    char cmd[64], arg1[64], arg2[64];

    while (1) {
        printf("db> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* 개행 제거 */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        /* 명령 파싱 */
        int argc = sscanf(line, "%63s %63s %63s", cmd, arg1, arg2);

        /* 대소문자 통일 */
        for (char *c = cmd;  *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        for (char *c = arg1; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;

        if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0) {
            printf("종료합니다.\n");
            break;

        } else if (strcmp(cmd, "HELP") == 0) {
            cmd_help();

        } else if (strcmp(cmd, "COUNT") == 0) {
            cmd_count(t);

        } else if (strcmp(cmd, "INSERT") == 0) {
            if (argc < 3) { printf("  사용법: INSERT <name> <win_rate> [rank]\n"); continue; }
            {
                char name[64], win_rate_s[64], rank[64] = "";
                double win_rate;

                if (sscanf(line, "%*s %63s %63s %63s", name, win_rate_s, rank) < 2) {
                    printf("  사용법: INSERT <name> <win_rate> [rank]\n");
                    continue;
                }
                win_rate = atof(win_rate_s);
                cmd_insert(t, name, win_rate, rank);
            }

        } else if (strcmp(cmd, "SELECT") == 0) {
            if (argc < 3) { printf("  사용법: SELECT ID <n>  또는  SELECT NAME <name>  또는  SELECT WINRATE <n>\n"); continue; }
            if (strcmp(arg1, "ID") == 0) {
                cmd_select_id(t, atoi(arg2));
            } else if (strcmp(arg1, "NAME") == 0) {
                cmd_select_name(t, arg2);
            } else if (strcmp(arg1, "WINRATE") == 0) {
                cmd_select_win_rate(t, atof(arg2));
            } else {
                printf("  알 수 없는 SELECT 대상: %s\n", arg1);
            }

        } else if (strcmp(cmd, "LOAD") == 0) {
            if (argc < 2) { printf("  사용법: LOAD <n>\n"); continue; }
            cmd_load(t, atoi(arg1));

        } else if (strcmp(cmd, "LOADCSV") == 0) {
            char path[256];
            if (sscanf(line, "%*s %255s", path) == 1) {
                cmd_load_csv(t, path);
            } else {
                cmd_load_csv(t, DEFAULT_CSV_PATH);
            }

        } else if (strcmp(cmd, "LIST") == 0) {
            int limit = (argc >= 2) ? atoi(arg1) : 10;
            cmd_list(t, limit);

        } else if (strcmp(cmd, "BENCH") == 0) {
            cmd_bench(t);

        } else {
            printf("  알 수 없는 명령: %s  (HELP 참조)\n", cmd);
        }
    }

    bptree_free(t->index);
    free(t->records);
    free(t);
    return 0;
}
