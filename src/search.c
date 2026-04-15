/*
 * search.c — 단일/범위 탐색 실시간 측정 후 JSON 출력
 *
 * 사용법:
 *   ./bin/search single <size> <target_id>
 *   ./bin/search range  <size> <lo> <hi>
 *
 * 출력: JSON (stdout)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "linear/linear_search.h"
#include "btree/btree.h"
#include "bplus_tree/bplus_tree.h"

/* 전역 연산 횟수 카운터 */
long long g_op_count = 0;

#define ITERS 5

static long long now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* 데이터 생성 */
static Player *make_table(int n) {
    Player *table = malloc(sizeof(Player) * n);
    if (!table) return NULL;
    srand(42);
    for (int i = 0; i < n; i++) {
        double win_rate;

        table[i].id = i + 1;
        snprintf(table[i].name, 32, "Player_%d", i);
        win_rate = 42.0 + (rand() % 5700) / 100.0;
        table[i].win_rate = win_rate;
        snprintf(
            table[i].rank,
            sizeof(table[i].rank),
            "%s",
            win_rate >= 85.0 ? "챌린저" :
            win_rate >= 75.0 ? "다이아" :
            win_rate >= 65.0 ? "플래티넘" :
            win_rate >= 55.0 ? "골드" : "실버"
        );
    }
    return table;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stdout, "{\"error\":\"인수 부족\"}\n");
        return 1;
    }

    const char *mode = argv[1];
    int n = atoi(argv[2]);

    if (n <= 0 || n > 10000000) {
        fprintf(stdout, "{\"error\":\"size 범위 초과\"}\n");
        return 1;
    }

    /* 데이터 생성 및 트리 삽입 */
    Player *table = make_table(n);
    if (!table) { fprintf(stdout, "{\"error\":\"메모리 부족\"}\n"); return 1; }

    BTree  *bt  = btree_create();
    BPTree *bpt = bptree_create();
    for (int i = 0; i < n; i++) {
        btree_insert(bt,   table[i].id, &table[i]);
        bptree_insert(bpt, table[i].id, &table[i]);
    }

    long long t0, t1;
    long long ls=0, bs=0, bps=0;
    long long ls_ops=0, bs_ops=0, bps_ops=0;

    if (strcmp(mode, "single") == 0) {
        int target = atoi(argv[3]);
        if (target <= 0) target = n / 2;

        for (int i = 0; i < ITERS; i++) {
            g_op_count=0; t0=now_us(); linear_search(table, n, target); t1=now_us();
            ls+=t1-t0; ls_ops+=g_op_count;

            g_op_count=0; t0=now_us(); btree_search(bt, target);         t1=now_us();
            bs+=t1-t0; bs_ops+=g_op_count;

            g_op_count=0; t0=now_us(); bptree_search(bpt, target);       t1=now_us();
            bps+=t1-t0; bps_ops+=g_op_count;
        }

        fprintf(stdout,
            "{"
            "\"mode\":\"single\","
            "\"size\":%d,"
            "\"target\":%d,"
            "\"linear_time\":%lld,\"linear_ops\":%lld,"
            "\"btree_time\":%lld,\"btree_ops\":%lld,"
            "\"bptree_time\":%lld,\"bptree_ops\":%lld"
            "}\n",
            n, target,
            ls/ITERS, ls_ops/ITERS,
            bs/ITERS, bs_ops/ITERS,
            bps/ITERS, bps_ops/ITERS
        );

    } else if (strcmp(mode, "range") == 0) {
        if (argc < 5) { fprintf(stdout, "{\"error\":\"range에 lo hi 필요\"}\n"); return 1; }
        int lo = atoi(argv[3]);
        int hi = atoi(argv[4]);
        if (lo <= 0) lo = 1;
        if (hi <= 0 || hi < lo) hi = (int)(n * 0.01);

        long long lr=0, br=0, bpr=0;
        long long lr_ops=0, br_ops=0, bpr_ops=0;

        for (int i = 0; i < ITERS; i++) {
            g_op_count=0; t0=now_us(); linear_range(table, n, lo, hi); t1=now_us();
            lr+=t1-t0; lr_ops+=g_op_count;

            g_op_count=0; t0=now_us(); btree_range(bt, lo, hi);         t1=now_us();
            br+=t1-t0; br_ops+=g_op_count;

            g_op_count=0; t0=now_us(); bptree_range(bpt, lo, hi);       t1=now_us();
            bpr+=t1-t0; bpr_ops+=g_op_count;
        }

        fprintf(stdout,
            "{"
            "\"mode\":\"range\","
            "\"size\":%d,"
            "\"lo\":%d,\"hi\":%d,"
            "\"linear_time\":%lld,\"linear_ops\":%lld,"
            "\"btree_time\":%lld,\"btree_ops\":%lld,"
            "\"bptree_time\":%lld,\"bptree_ops\":%lld"
            "}\n",
            n, lo, hi,
            lr/ITERS, lr_ops/ITERS,
            br/ITERS, br_ops/ITERS,
            bpr/ITERS, bpr_ops/ITERS
        );

    } else {
        fprintf(stdout, "{\"error\":\"알 수 없는 mode: %s\"}\n", mode);
    }

    btree_free(bt);
    bptree_free(bpt);
    free(table);
    return 0;
}
