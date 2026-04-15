#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../common/dataset_io.h"
#include "../common/player.h"
#include "../linear/linear_search.h"
#include "../btree/btree.h"
#include "../bplus_tree/bplus_tree.h"

long long g_op_count = 0;

typedef struct {
    double avg_us;
    long long ops;
} TimingResult;

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static TimingResult benchmark_single_linear(Player *players, int count, int target_id, int iterations) {
    int i;
    long long total = 0;
    long long ops_total = 0;

    for (i = 0; i < iterations; ++i) {
        long long start = now_ns();
        g_op_count = 0;
        (void)linear_search(players, count, target_id);
        total += now_ns() - start;
        ops_total += g_op_count;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static TimingResult benchmark_single_linear_name(
    Player *players,
    int count,
    const char *target_name,
    int iterations,
    int *resolved_id
) {
    int i;
    long long total = 0;
    long long ops_total = 0;
    int found_id = 0;

    for (i = 0; i < iterations; ++i) {
        Player *found;
        long long start = now_ns();

        g_op_count = 0;
        found = linear_search_name(players, count, target_name);
        total += now_ns() - start;
        ops_total += g_op_count;

        if (found) {
            found_id = found->id;
        }
    }

    if (resolved_id) {
        *resolved_id = found_id;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static TimingResult benchmark_single_btree(BTree *tree, int target_id, int iterations) {
    int i;
    long long total = 0;
    long long ops_total = 0;

    for (i = 0; i < iterations; ++i) {
        long long start = now_ns();
        g_op_count = 0;
        (void)btree_search(tree, target_id);
        total += now_ns() - start;
        ops_total += g_op_count;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static TimingResult benchmark_single_bptree(BPTree *tree, int target_id, int iterations) {
    int i;
    long long total = 0;
    long long ops_total = 0;

    for (i = 0; i < iterations; ++i) {
        long long start = now_ns();
        g_op_count = 0;
        (void)bptree_search(tree, target_id);
        total += now_ns() - start;
        ops_total += g_op_count;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static TimingResult benchmark_range_linear(Player *players, int count, int lo, int hi, int iterations) {
    int i;
    long long total = 0;
    long long ops_total = 0;

    for (i = 0; i < iterations; ++i) {
        long long start = now_ns();
        g_op_count = 0;
        (void)linear_range(players, count, lo, hi);
        total += now_ns() - start;
        ops_total += g_op_count;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static TimingResult benchmark_range_btree(BTree *tree, int lo, int hi, int iterations) {
    int i;
    long long total = 0;
    long long ops_total = 0;

    for (i = 0; i < iterations; ++i) {
        long long start = now_ns();
        g_op_count = 0;
        (void)btree_range(tree, lo, hi);
        total += now_ns() - start;
        ops_total += g_op_count;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static TimingResult benchmark_range_bptree(BPTree *tree, int lo, int hi, int iterations) {
    int i;
    long long total = 0;
    long long ops_total = 0;

    for (i = 0; i < iterations; ++i) {
        long long start = now_ns();
        g_op_count = 0;
        (void)bptree_range(tree, lo, hi);
        total += now_ns() - start;
        ops_total += g_op_count;
    }

    {
        TimingResult result;
        result.avg_us = (double)total / iterations / 1000.0;
        result.ops = ops_total / iterations;
        return result;
    }
}

static int compare_players_desc(const void *lhs, const void *rhs) {
    const Player *a = (const Player *)lhs;
    const Player *b = (const Player *)rhs;
    if (b->win_rate > a->win_rate) {
        return 1;
    }
    if (b->win_rate < a->win_rate) {
        return -1;
    }
    return a->id - b->id;
}

static void current_timestamp(char *buffer, size_t size) {
    time_t t = time(NULL);
    struct tm tm_value;

#if defined(_WIN32)
    localtime_s(&tm_value, &t);
#else
    localtime_r(&t, &tm_value);
#endif
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm_value);
}

static void print_json_string(FILE *out, const char *text) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");

    fputc('"', out);
    while (*cursor) {
        switch (*cursor) {
            case '\"':
                fputs("\\\"", out);
                break;
            case '\\':
                fputs("\\\\", out);
                break;
            case '\b':
                fputs("\\b", out);
                break;
            case '\f':
                fputs("\\f", out);
                break;
            case '\n':
                fputs("\\n", out);
                break;
            case '\r':
                fputs("\\r", out);
                break;
            case '\t':
                fputs("\\t", out);
                break;
            default:
                if (*cursor < 0x20) {
                    fprintf(out, "\\u%04x", *cursor);
                } else {
                    fputc(*cursor, out);
                }
                break;
        }
        cursor++;
    }
    fputc('"', out);
}

static int write_json(
    const char *path,
    int count,
    int iterations,
    int target_id,
    const char *target_name,
    int resolved_name_id,
    int lo,
    int hi,
    int range_count,
    TimingResult single_linear,
    TimingResult single_btree,
    TimingResult single_bptree,
    TimingResult name_linear,
    TimingResult name_btree,
    TimingResult name_bptree,
    TimingResult range_linear,
    TimingResult range_btree,
    TimingResult range_bptree,
    Player *players
) {
    FILE *fp = fopen(path, "w");
    Player *copy;
    char timestamp[32];
    int i;

    if (!fp) {
        return 0;
    }

    copy = (Player *)malloc(sizeof(Player) * count);
    if (!copy) {
        fclose(fp);
        return 0;
    }

    memcpy(copy, players, sizeof(Player) * count);
    qsort(copy, count, sizeof(Player), compare_players_desc);
    current_timestamp(timestamp, sizeof(timestamp));

    fprintf(fp, "{\n");
    fprintf(fp, "  \"meta\": {\n");
    fprintf(fp, "    \"generated_at\": \"%s\",\n", timestamp);
    fprintf(fp, "    \"dataset_size\": %d,\n", count);
    fprintf(fp, "    \"iterations\": %d\n", iterations);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"single_search\": {\n");
    fprintf(fp, "    \"target_id\": %d,\n", target_id);
    fprintf(fp, "    \"linear\": {\"avg_us\": %.3f, \"ops\": %lld},\n", single_linear.avg_us, single_linear.ops);
    fprintf(fp, "    \"btree\": {\"avg_us\": %.3f, \"ops\": %lld},\n", single_btree.avg_us, single_btree.ops);
    fprintf(fp, "    \"bptree\": {\"avg_us\": %.3f, \"ops\": %lld}\n", single_bptree.avg_us, single_bptree.ops);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"name_bridge_search\": {\n");
    fprintf(fp, "    \"target_name\": ");
    print_json_string(fp, target_name);
    fprintf(fp, ",\n");
    fprintf(fp, "    \"resolved_id\": %d,\n", resolved_name_id);
    fprintf(fp, "    \"note\": ");
    print_json_string(fp, "name lookup is resolved by linear scan first, then B-Tree/B+Tree search reuse the resolved id");
    fprintf(fp, ",\n");
    fprintf(fp, "    \"linear_name\": {\"avg_us\": %.3f, \"ops\": %lld},\n", name_linear.avg_us, name_linear.ops);
    fprintf(fp, "    \"btree_by_id\": {\"avg_us\": %.3f, \"ops\": %lld},\n", name_btree.avg_us, name_btree.ops);
    fprintf(fp, "    \"bptree_by_id\": {\"avg_us\": %.3f, \"ops\": %lld}\n", name_bptree.avg_us, name_bptree.ops);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"range_search\": {\n");
    fprintf(fp, "    \"lo\": %d,\n", lo);
    fprintf(fp, "    \"hi\": %d,\n", hi);
    fprintf(fp, "    \"count\": %d,\n", range_count);
    fprintf(fp, "    \"linear\": {\"avg_us\": %.3f, \"ops\": %lld},\n", range_linear.avg_us, range_linear.ops);
    fprintf(fp, "    \"btree\": {\"avg_us\": %.3f, \"ops\": %lld},\n", range_btree.avg_us, range_btree.ops);
    fprintf(fp, "    \"bptree\": {\"avg_us\": %.3f, \"ops\": %lld}\n", range_bptree.avg_us, range_bptree.ops);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"top_players\": [\n");
    for (i = 0; i < 10 && i < count; ++i) {
        fprintf(fp, "    {\"id\": %d, \"nickname\": ", copy[i].id);
        print_json_string(fp, copy[i].name);
        fprintf(fp, ", \"win_rate\": %.2f, \"rank\": ", copy[i].win_rate);
        print_json_string(fp, copy[i].rank);
        fprintf(fp, "}%s\n", (i == 9 || i == count - 1) ? "" : ",");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    free(copy);
    fclose(fp);
    return 1;
}

int main(int argc, char **argv) {
    const char *csv_path;
    const char *output_path = NULL;
    Player *players;
    BTree *btree;
    BPTree *bptree;
    int count;
    int i;
    int iterations = 5;
    int target_id;
    const char *target_name;
    int resolved_name_id;
    int lo;
    int hi;
    int range_count;
    TimingResult single_linear;
    TimingResult single_btree;
    TimingResult single_bptree;
    TimingResult name_linear;
    TimingResult name_btree;
    TimingResult name_bptree;
    TimingResult range_linear;
    TimingResult range_btree;
    TimingResult range_bptree;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <csv_path> [output_json]\n", argv[0]);
        return 1;
    }

    csv_path = argv[1];
    if (argc >= 3) {
        output_path = argv[2];
    }

    players = load_players_csv(csv_path, &count);
    btree = btree_create();
    bptree = bptree_create();
    if (!players || count <= 0 || !btree || !bptree) {
        fprintf(stderr, "failed to load csv or allocate memory\n");
        free(players);
        btree_free(btree);
        bptree_free(bptree);
        return 1;
    }

    for (i = 0; i < count; ++i) {
        btree_insert(btree, players[i].id, &players[i]);
        bptree_insert(bptree, players[i].id, &players[i]);
    }

    target_id = count / 2;
    if (target_id < 1) {
        target_id = 1;
    }
    target_name = players[target_id - 1].name;
    lo = target_id;
    hi = target_id + 999;
    if (hi > count) {
        hi = count;
    }

    range_count = linear_range(players, count, lo, hi);
    single_linear = benchmark_single_linear(players, count, target_id, iterations);
    single_btree = benchmark_single_btree(btree, target_id, iterations);
    single_bptree = benchmark_single_bptree(bptree, target_id, iterations);
    name_linear = benchmark_single_linear_name(players, count, target_name, iterations, &resolved_name_id);
    name_btree = benchmark_single_btree(btree, resolved_name_id, iterations);
    name_bptree = benchmark_single_bptree(bptree, resolved_name_id, iterations);
    range_linear = benchmark_range_linear(players, count, lo, hi, iterations);
    range_btree = benchmark_range_btree(btree, lo, hi, iterations);
    range_bptree = benchmark_range_bptree(bptree, lo, hi, iterations);

    printf("Dataset size: %d\n", count);
    printf("Single search target: id=%d\n", target_id);
    printf("  Linear : %.3f us\n", single_linear.avg_us);
    printf("  B-Tree : %.3f us\n", single_btree.avg_us);
    printf("  B+Tree : %.3f us\n", single_bptree.avg_us);
    printf("Name bridge target: name=%s -> resolved id=%d\n", target_name, resolved_name_id);
    printf("  Linear(name)        : %.3f us\n", name_linear.avg_us);
    printf("  B-Tree(resolved id) : %.3f us\n", name_btree.avg_us);
    printf("  B+Tree(resolved id) : %.3f us\n", name_bptree.avg_us);
    printf("Range search: %d..%d (%d rows)\n", lo, hi, range_count);
    printf("  Linear : %.3f us\n", range_linear.avg_us);
    printf("  B-Tree : %.3f us\n", range_btree.avg_us);
    printf("  B+Tree : %.3f us\n", range_bptree.avg_us);

    if (output_path && !write_json(
        output_path,
        count,
        iterations,
        target_id,
        target_name,
        resolved_name_id,
        lo,
        hi,
        range_count,
        single_linear,
        single_btree,
        single_bptree,
        name_linear,
        name_btree,
        name_bptree,
        range_linear,
        range_btree,
        range_bptree,
        players
    )) {
        fprintf(stderr, "failed to write json: %s\n", output_path);
        free(players);
        btree_free(btree);
        bptree_free(bptree);
        return 1;
    }

    free(players);
    btree_free(btree);
    bptree_free(bptree);
    return 0;
}
