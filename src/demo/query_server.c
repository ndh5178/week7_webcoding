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

#define SEARCH_ITERS 5
#define TOP_LIMIT 10
#define DEFAULT_PAGE_SIZE 10
#define MAX_PAGE_SIZE 50

long long g_op_count = 0;

typedef struct {
    Player *items[TOP_LIMIT];
    int count;
} TopPlayers;

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

static void print_player_json(Player *player) {
    printf("{");
    printf("\"id\":%d,", player->id);
    printf("\"nickname\":");
    print_json_string(stdout, player->name);
    printf(",");
    printf("\"win_rate\":%.2f,", player->win_rate);
    printf("\"rank\":");
    print_json_string(stdout, player->rank);
    printf("}");
}

static int normalize_page_size(int page_size) {
    if (page_size <= 0) {
        return DEFAULT_PAGE_SIZE;
    }
    if (page_size > MAX_PAGE_SIZE) {
        return MAX_PAGE_SIZE;
    }
    return page_size;
}

static int player_better(const Player *candidate, const Player *current) {
    if (!current) {
        return 1;
    }
    if (candidate->win_rate > current->win_rate) {
        return 1;
    }
    if (candidate->win_rate < current->win_rate) {
        return 0;
    }
    return candidate->id < current->id;
}

static void top_players_reset(TopPlayers *top) {
    memset(top, 0, sizeof(*top));
}

static void top_players_consider(TopPlayers *top, Player *player) {
    int pos;
    int limit;
    int i;

    if (!player) {
        return;
    }

    g_op_count++;
    pos = top->count;
    while (pos > 0) {
        g_op_count++;
        if (!player_better(player, top->items[pos - 1])) {
            break;
        }
        pos--;
    }

    if (top->count < TOP_LIMIT) {
        for (i = top->count; i > pos; --i) {
            top->items[i] = top->items[i - 1];
        }
        top->items[pos] = player;
        top->count += 1;
        return;
    }

    if (pos >= TOP_LIMIT) {
        return;
    }

    limit = TOP_LIMIT - 1;
    for (i = limit; i > pos; --i) {
        top->items[i] = top->items[i - 1];
    }
    top->items[pos] = player;
}

static void collect_top_linear(Player *players, int count, TopPlayers *top) {
    int i;

    for (i = 0; i < count; ++i) {
        top_players_consider(top, &players[i]);
    }
}

static void collect_top_btree_leaves(BTNode *node, TopPlayers *top) {
    int i;

    if (!node) {
        return;
    }

    if (node->is_leaf) {
        g_op_count++;
        for (i = 0; i < node->num_keys; ++i) {
            top_players_consider(top, (Player *)node->ptrs[i]);
        }
        return;
    }

    for (i = 0; i <= node->num_keys; ++i) {
        g_op_count++;
        collect_top_btree_leaves(node->children[i], top);
    }
}

static void collect_top_btree(BTree *tree, TopPlayers *top) {
    if (!tree || !tree->root) {
        return;
    }

    collect_top_btree_leaves(tree->root, top);
}

static BPLeaf *leftmost_bptree_leaf(BPTree *tree) {
    BPNode *node;

    if (!tree || !tree->root) {
        return NULL;
    }

    node = tree->root;
    while (node && !node->is_leaf) {
        g_op_count++;
        node = node->children[0];
    }

    return node ? node->leaf : NULL;
}

static void collect_top_bptree(BPTree *tree, TopPlayers *top) {
    BPLeaf *leaf = leftmost_bptree_leaf(tree);

    while (leaf) {
        int i;

        g_op_count++;
        for (i = 0; i < leaf->num_keys; ++i) {
            top_players_consider(top, (Player *)leaf->ptrs[i]);
        }
        leaf = leaf->next;
    }
}

static void print_top_players_json(const TopPlayers *top) {
    int i;

    printf("[");
    for (i = 0; i < top->count; ++i) {
        if (i > 0) {
            printf(",");
        }

        print_player_json(top->items[i]);
    }
    printf("]");
}

static void print_search_result(Player *players, int count, BTree *btree, BPTree *bptree, int target_id) {
    Player *linear_found;
    Player *btree_found;
    Player *bptree_found;
    long long linear_total = 0;
    long long btree_total = 0;
    long long bptree_total = 0;
    long long linear_ops_total = 0;
    long long btree_ops_total = 0;
    long long bptree_ops_total = 0;
    int i;

    linear_found = NULL;
    btree_found = NULL;
    bptree_found = NULL;

    for (i = 0; i < SEARCH_ITERS; ++i) {
        long long start;

        g_op_count = 0;
        start = now_ns();
        linear_found = linear_search(players, count, target_id);
        linear_total += now_ns() - start;
        linear_ops_total += g_op_count;

        g_op_count = 0;
        start = now_ns();
        btree_found = (Player *)btree_search(btree, target_id);
        btree_total += now_ns() - start;
        btree_ops_total += g_op_count;

        g_op_count = 0;
        start = now_ns();
        bptree_found = (Player *)bptree_search(bptree, target_id);
        bptree_total += now_ns() - start;
        bptree_ops_total += g_op_count;
    }

    printf("{");
    printf("\"ok\":true,");
    printf("\"dataset_size\":%d,", count);
    printf("\"target_id\":%d,", target_id);
    printf("\"found\":%s,", linear_found ? "true" : "false");
    printf("\"timings\":{");
    printf("\"linear_us\":%.3f,", (double)linear_total / SEARCH_ITERS / 1000.0);
    printf("\"btree_us\":%.3f,", (double)btree_total / SEARCH_ITERS / 1000.0);
    printf("\"bptree_us\":%.3f", (double)bptree_total / SEARCH_ITERS / 1000.0);
    printf("},");
    printf("\"linear_ops\":%lld,", linear_ops_total / SEARCH_ITERS);
    printf("\"btree_ops\":%lld,", btree_ops_total / SEARCH_ITERS);
    printf("\"bptree_ops\":%lld,", bptree_ops_total / SEARCH_ITERS);

    if (linear_found && btree_found && bptree_found) {
        printf("\"player\":");
        print_player_json(linear_found);
    } else {
        printf("\"player\":null");
    }

    printf("}\n");
    fflush(stdout);
}

static int collect_range_page(
    Player *players,
    int count,
    int lo,
    int hi,
    int offset,
    int page_size,
    Player **page_players
) {
    int matched = 0;
    int page_count = 0;
    int i;

    for (i = 0; i < count; ++i) {
        if (players[i].id < lo || players[i].id > hi) {
            continue;
        }

        if (matched >= offset && page_count < page_size) {
            page_players[page_count++] = &players[i];
        }

        matched += 1;
        if (page_count == page_size) {
            continue;
        }
    }

    return page_count;
}

static void print_range_players_json(Player **page_players, int page_items) {
    int i;

    printf("[");
    for (i = 0; i < page_items; ++i) {
        if (i > 0) {
            printf(",");
        }
        print_player_json(page_players[i]);
    }
    printf("]");
}

static void print_range_result(
    Player *players,
    int count,
    BTree *btree,
    BPTree *bptree,
    int lo,
    int hi,
    int requested_page,
    int requested_page_size
) {
    int linear_count = 0;
    int btree_count = 0;
    int bptree_count = 0;
    long long linear_total = 0;
    long long btree_total = 0;
    long long bptree_total = 0;
    long long linear_ops_total = 0;
    long long btree_ops_total = 0;
    long long bptree_ops_total = 0;
    int page_size;
    int page_count;
    int page;
    int offset;
    int page_items = 0;
    Player **page_players = NULL;
    int i;

    for (i = 0; i < SEARCH_ITERS; ++i) {
        long long start;

        g_op_count = 0;
        start = now_ns();
        linear_count = linear_range(players, count, lo, hi);
        linear_total += now_ns() - start;
        linear_ops_total += g_op_count;

        g_op_count = 0;
        start = now_ns();
        btree_count = btree_range(btree, lo, hi);
        btree_total += now_ns() - start;
        btree_ops_total += g_op_count;

        g_op_count = 0;
        start = now_ns();
        bptree_count = bptree_range(bptree, lo, hi);
        bptree_total += now_ns() - start;
        bptree_ops_total += g_op_count;
    }

    page_size = normalize_page_size(requested_page_size);
    page_count = linear_count > 0 ? (linear_count + page_size - 1) / page_size : 0;
    page = requested_page > 0 ? requested_page : 1;
    if (page_count > 0 && page > page_count) {
        page = page_count;
    }

    offset = (page - 1) * page_size;
    page_players = (Player **)calloc((size_t)page_size, sizeof(Player *));
    if (page_players) {
        page_items = collect_range_page(players, count, lo, hi, offset, page_size, page_players);
    }

    printf("{");
    printf("\"ok\":true,");
    printf("\"size\":%d,", count);
    printf("\"lo\":%d,", lo);
    printf("\"hi\":%d,", hi);
    printf("\"range_count\":%d,", linear_count);
    printf("\"linear_time\":%.3f,", (double)linear_total / SEARCH_ITERS / 1000.0);
    printf("\"linear_ops\":%lld,", linear_ops_total / SEARCH_ITERS);
    printf("\"btree_time\":%.3f,", (double)btree_total / SEARCH_ITERS / 1000.0);
    printf("\"btree_ops\":%lld,", btree_ops_total / SEARCH_ITERS);
    printf("\"bptree_time\":%.3f,", (double)bptree_total / SEARCH_ITERS / 1000.0);
    printf("\"bptree_ops\":%lld,", bptree_ops_total / SEARCH_ITERS);
    printf("\"page\":%d,", page);
    printf("\"page_size\":%d,", page_size);
    printf("\"page_count\":%d,", page_count);
    printf("\"counts_match\":%s,", (linear_count == btree_count && linear_count == bptree_count) ? "true" : "false");
    printf("\"players\":");
    print_range_players_json(page_players, page_items);
    printf("}\n");
    fflush(stdout);
    free(page_players);
}

static void print_top_result(Player *players, int count, BTree *btree, BPTree *bptree) {
    TopPlayers linear_top;
    TopPlayers btree_top;
    TopPlayers bptree_top;
    long long linear_total = 0;
    long long btree_total = 0;
    long long bptree_total = 0;
    long long linear_ops_total = 0;
    long long btree_ops_total = 0;
    long long bptree_ops_total = 0;
    int i;

    top_players_reset(&linear_top);
    top_players_reset(&btree_top);
    top_players_reset(&bptree_top);

    for (i = 0; i < SEARCH_ITERS; ++i) {
        long long start;

        g_op_count = 0;
        top_players_reset(&linear_top);
        start = now_ns();
        collect_top_linear(players, count, &linear_top);
        linear_total += now_ns() - start;
        linear_ops_total += g_op_count;

        g_op_count = 0;
        top_players_reset(&btree_top);
        start = now_ns();
        collect_top_btree(btree, &btree_top);
        btree_total += now_ns() - start;
        btree_ops_total += g_op_count;

        g_op_count = 0;
        top_players_reset(&bptree_top);
        start = now_ns();
        collect_top_bptree(bptree, &bptree_top);
        bptree_total += now_ns() - start;
        bptree_ops_total += g_op_count;
    }

    printf("{");
    printf("\"ok\":true,");
    printf("\"size\":%d,", count);
    printf("\"top_count\":%d,", linear_top.count);
    printf("\"linear_time\":%.3f,", (double)linear_total / SEARCH_ITERS / 1000.0);
    printf("\"linear_ops\":%lld,", linear_ops_total / SEARCH_ITERS);
    printf("\"btree_time\":%.3f,", (double)btree_total / SEARCH_ITERS / 1000.0);
    printf("\"btree_ops\":%lld,", btree_ops_total / SEARCH_ITERS);
    printf("\"bptree_time\":%.3f,", (double)bptree_total / SEARCH_ITERS / 1000.0);
    printf("\"bptree_ops\":%lld,", bptree_ops_total / SEARCH_ITERS);
    printf("\"top_players\":");
    print_top_players_json(&linear_top);
    printf("}\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    const char *csv_path;
    Player *players;
    BTree *btree;
    BPTree *bptree;
    int count = 0;
    int i;
    char line[128];

    if (argc < 2) {
        fprintf(stderr, "usage: %s <csv_path>\n", argv[0]);
        return 1;
    }

    csv_path = argv[1];
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

    printf("{\"ok\":true,\"ready\":true,\"dataset_size\":%d}\n", count);
    fflush(stdout);

    while (fgets(line, sizeof(line), stdin)) {
        int target_id = 0;
        int lo = 0;
        int hi = 0;
        int page = 1;
        int page_size = DEFAULT_PAGE_SIZE;
        int parsed;

        if (strncmp(line, "search ", 7) == 0) {
            target_id = atoi(line + 7);
            if (target_id <= 0) {
                printf("{\"ok\":false,\"message\":\"id must be positive\"}\n");
                fflush(stdout);
                continue;
            }

            print_search_result(players, count, btree, bptree, target_id);
            continue;
        }

        parsed = sscanf(line, "range %d %d %d %d", &lo, &hi, &page, &page_size);
        if (parsed >= 2) {
            if (lo <= 0 || hi <= 0 || hi < lo || page <= 0 || page_size <= 0) {
                printf("{\"ok\":false,\"message\":\"lo/hi must be positive and hi >= lo\"}\n");
                fflush(stdout);
                continue;
            }

            if (parsed < 3) {
                page = 1;
            }
            if (parsed < 4) {
                page_size = DEFAULT_PAGE_SIZE;
            }

            print_range_result(players, count, btree, bptree, lo, hi, page, page_size);
            continue;
        }

        if (strncmp(line, "top", 3) == 0) {
            print_top_result(players, count, btree, bptree);
            continue;
        }

        if (strncmp(line, "quit", 4) == 0) {
            break;
        }

        printf("{\"ok\":false,\"message\":\"unknown command\"}\n");
        fflush(stdout);
    }

    free(players);
    btree_free(btree);
    bptree_free(bptree);
    return 0;
}
