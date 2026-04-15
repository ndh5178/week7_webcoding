#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../common/dataset_io.h"
#include "../common/player.h"
#include "../linear/linear_search.h"
#include "../btree/btree.h"
#include "../bplus_tree/bplus_tree.h"

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
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

int main(int argc, char **argv) {
    int count;
    int target_id;
    const char *csv_path;
    Player *players;
    BTree *btree;
    BPTree *bptree;
    Player *linear_found;
    Player *btree_found;
    Player *bptree_found;
    long long start;
    double linear_us;
    double btree_us;
    double bptree_us;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <csv_path> <target_id>\n", argv[0]);
        return 1;
    }

    csv_path = argv[1];
    target_id = atoi(argv[2]);
    if (target_id <= 0) {
        fprintf(stderr, "target_id must be positive\n");
        return 1;
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

    start = now_ns();
    linear_found = linear_search(players, count, target_id);
    linear_us = (double)(now_ns() - start) / 1000.0;

    start = now_ns();
    btree_found = (Player *)btree_search(btree, target_id);
    btree_us = (double)(now_ns() - start) / 1000.0;

    start = now_ns();
    bptree_found = (Player *)bptree_search(bptree, target_id);
    bptree_us = (double)(now_ns() - start) / 1000.0;

    printf("{");
    printf("\"ok\":true,");
    printf("\"dataset_size\":%d,", count);
    printf("\"target_id\":%d,", target_id);
    printf("\"found\":%s,", linear_found ? "true" : "false");
    printf("\"timings\":{");
    printf("\"linear_us\":%.3f,", linear_us);
    printf("\"btree_us\":%.3f,", btree_us);
    printf("\"bptree_us\":%.3f", bptree_us);
    printf("},");

    if (linear_found && btree_found && bptree_found) {
        printf("\"player\":{");
        printf("\"id\":%d,", linear_found->id);
        printf("\"nickname\":");
        print_json_string(stdout, linear_found->name);
        printf(",");
        printf("\"win_rate\":%.2f,", linear_found->win_rate);
        printf("\"rank\":");
        print_json_string(stdout, linear_found->rank);
        printf("}");
    } else {
        printf("\"player\":null");
    }

    printf("}\n");

    free(players);
    btree_free(btree);
    bptree_free(bptree);
    return 0;
}
