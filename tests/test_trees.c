#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/common/player.h"
#include "../src/btree/btree.h"
#include "../src/bplus_tree/bplus_tree.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(condition, label)                                                     \
    do {                                                                            \
        if (condition) {                                                            \
            printf("  [PASS] %s\n", label);                                         \
            tests_passed += 1;                                                      \
        } else {                                                                    \
            printf("  [FAIL] %s\n", label);                                         \
            tests_failed += 1;                                                      \
        }                                                                           \
    } while (0)

static void fill_players(Player *players, int count) {
    int i;

    for (i = 0; i < count; ++i) {
        players[i].id = i + 1;
        snprintf(players[i].name, sizeof(players[i].name), "Player_%02d", i + 1);
        players[i].win_rate = 42.0 + ((i * 17) % 4500) / 100.0;
        snprintf(players[i].rank, sizeof(players[i].rank), "Rank_%d", (i % 5) + 1);
    }
}

static void make_temp_path(char *buffer, size_t size, const char *label) {
    snprintf(buffer, size, "/tmp/week7_webcoding_%ld_%s", (long)getpid(), label);
}

static int write_escape_fixture_csv(const char *path) {
    FILE *fp = fopen(path, "w");

    if (!fp) {
        return 0;
    }

    fprintf(fp, "id,닉네임,승률(%%),랭크\n");
    fprintf(fp, "1,A\"B,55.00,Gold\\Tier\n");
    fprintf(fp, "2,Slash\\Name,66.50,Dia\"mond\n");

    return fclose(fp) == 0;
}

static int write_top_fixture_csv(const char *path) {
    FILE *fp = fopen(path, "w");

    if (!fp) {
        return 0;
    }

    fprintf(fp, "id,닉네임,승률(%%),랭크\n");
    fprintf(fp, "1,Alpha,70.00,플래티넘\n");
    fprintf(fp, "2,Beta,91.20,챌린저\n");
    fprintf(fp, "3,Gamma,88.88,다이아\n");
    fprintf(fp, "4,Delta,95.55,챌린저\n");
    fprintf(fp, "5,Epsilon,65.10,플래티넘\n");
    fprintf(fp, "6,Zeta,84.30,다이아\n");
    fprintf(fp, "7,Eta,77.70,다이아\n");
    fprintf(fp, "8,Theta,93.25,챌린저\n");
    fprintf(fp, "9,Iota,81.90,다이아\n");
    fprintf(fp, "10,Kappa,79.40,다이아\n");
    fprintf(fp, "11,Lambda,97.10,챌린저\n");
    fprintf(fp, "12,Mu,68.45,플래티넘\n");

    return fclose(fp) == 0;
}

static int write_range_fixture_csv(const char *path) {
    FILE *fp = fopen(path, "w");
    int i;

    if (!fp) {
        return 0;
    }

    fprintf(fp, "id,닉네임,승률(%%),랭크\n");
    for (i = 1; i <= 30; ++i) {
        fprintf(fp, "%d,Player_%02d,%.2f,골드\n", i, i, 50.0 + (i % 10));
    }

    return fclose(fp) == 0;
}

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    char *buffer;
    long size;

    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, fp) != (size_t)size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static int run_command_ok(const char *command) {
    int status = system(command);

    if (status == -1) {
        return 0;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void test_btree_single_search(void) {
    Player players[3];
    BTree *tree = btree_create();

    printf("\n[B-Tree single search]\n");
    fill_players(players, 3);

    btree_insert(tree, players[0].id, &players[0]);
    btree_insert(tree, players[1].id, &players[1]);
    btree_insert(tree, players[2].id, &players[2]);

    CHECK(((Player *)btree_search(tree, 1))->id == 1, "search finds id=1");
    CHECK(((Player *)btree_search(tree, 2))->id == 2, "search finds id=2");
    CHECK(((Player *)btree_search(tree, 3))->id == 3, "search finds id=3");
    CHECK(btree_search(tree, 99) == NULL, "missing id returns NULL");

    btree_free(tree);
}

static void test_btree_range_search(void) {
    Player players[10];
    BTree *tree = btree_create();
    int i;

    printf("\n[B-Tree range search]\n");
    fill_players(players, 10);

    for (i = 0; i < 10; ++i) {
        btree_insert(tree, players[i].id, &players[i]);
    }

    CHECK(btree_range(tree, 3, 7) == 5, "range 3..7 returns 5 rows");
    CHECK(btree_range(tree, 1, 10) == 10, "range 1..10 returns 10 rows");
    CHECK(btree_range(tree, 50, 60) == 0, "range outside keys returns 0");

    btree_free(tree);
}

static void test_btree_split_handling(void) {
    Player *players = (Player *)calloc(BT_ORDER * 3, sizeof(Player));
    BTree *tree = btree_create();
    int i;
    int ok = 1;

    printf("\n[B-Tree split handling]\n");
    fill_players(players, BT_ORDER * 3);

    for (i = 0; i < BT_ORDER * 3; ++i) {
        btree_insert(tree, players[i].id, &players[i]);
    }

    for (i = 0; i < BT_ORDER * 3; ++i) {
        Player *found = (Player *)btree_search(tree, players[i].id);
        if (!found || found->id != players[i].id) {
            ok = 0;
            break;
        }
    }

    CHECK(ok, "split keeps all keys searchable");

    btree_free(tree);
    free(players);
}

static void test_btree_range_after_split(void) {
    Player *players = (Player *)calloc(BT_ORDER * 3, sizeof(Player));
    BTree *tree = btree_create();
    int count = BT_ORDER * 3;
    int i;

    printf("\n[B-Tree range after split]\n");
    fill_players(players, count);

    for (i = 0; i < count; ++i) {
        btree_insert(tree, players[i].id, &players[i]);
    }

    CHECK(btree_range(tree, 1, count) == count, "full range count stays exact after splits");
    CHECK(btree_range(tree, 10, 20) == 11, "narrow range across split boundary stays exact");

    btree_free(tree);
    free(players);
}

static void test_btree_duplicate_key_update_after_split(void) {
    Player *players = (Player *)calloc(BT_ORDER * 3, sizeof(Player));
    Player replacement;
    BTree *tree = btree_create();
    Player *found;
    int i;

    printf("\n[B-Tree duplicate key update after split]\n");
    fill_players(players, BT_ORDER * 3);

    for (i = 0; i < BT_ORDER * 3; ++i) {
        btree_insert(tree, players[i].id, &players[i]);
    }

    replacement = players[32];
    replacement.win_rate = 99.99;
    snprintf(replacement.name, sizeof(replacement.name), "Replacement_33");
    snprintf(replacement.rank, sizeof(replacement.rank), "Rank_X");
    btree_insert(tree, replacement.id, &replacement);

    found = (Player *)btree_search(tree, replacement.id);
    CHECK(found == &replacement, "reinserted promoted key updates the stored pointer");
    CHECK(btree_range(tree, replacement.id, replacement.id) == 1, "reinserted promoted key is not duplicated");

    btree_free(tree);
    free(players);
}

static void test_query_demo_json_escaping(void) {
    char csv_path[128];
    char output_path[128];
    char command[320];
    char *output = NULL;
    int wrote_fixture;
    int ran_command = 0;

    printf("\n[query_demo JSON escaping]\n");
    make_temp_path(csv_path, sizeof(csv_path), "query_demo_fixture.csv");
    make_temp_path(output_path, sizeof(output_path), "query_demo_output.json");

    wrote_fixture = write_escape_fixture_csv(csv_path);
    if (wrote_fixture) {
        snprintf(command, sizeof(command), "./bin/query_demo %s 1 > %s", csv_path, output_path);
        ran_command = run_command_ok(command);
        if (ran_command) {
            output = read_text_file(output_path);
        }
    }

    CHECK(wrote_fixture, "fixture csv created for query_demo");
    CHECK(ran_command, "query_demo runs against escaped fixture");
    CHECK(output && strstr(output, "\"nickname\":\"A\\\"B\"") != NULL, "query_demo escapes quotes in nickname");
    CHECK(output && strstr(output, "\"rank\":\"Gold\\\\Tier\"") != NULL, "query_demo escapes backslashes in rank");
    CHECK(output && strstr(output, "\"nickname\":\"A\"B\"") == NULL, "query_demo avoids raw quote breakage");

    free(output);
    remove(csv_path);
    remove(output_path);
}

static void test_query_server_json_escaping(void) {
    char csv_path[128];
    char output_path[128];
    char command[384];
    char *output = NULL;
    int wrote_fixture;
    int ran_command = 0;

    printf("\n[query_server JSON escaping]\n");
    make_temp_path(csv_path, sizeof(csv_path), "query_server_fixture.csv");
    make_temp_path(output_path, sizeof(output_path), "query_server_output.log");

    wrote_fixture = write_escape_fixture_csv(csv_path);
    if (wrote_fixture) {
        snprintf(
            command,
            sizeof(command),
            "printf 'search 1\\nquit\\n' | ./bin/query_server %s > %s",
            csv_path,
            output_path
        );
        ran_command = run_command_ok(command);
        if (ran_command) {
            output = read_text_file(output_path);
        }
    }

    CHECK(wrote_fixture, "fixture csv created for query_server");
    CHECK(ran_command, "query_server runs against escaped fixture");
    CHECK(output && strstr(output, "\"nickname\":\"A\\\"B\"") != NULL, "query_server escapes quotes in nickname");
    CHECK(output && strstr(output, "\"rank\":\"Gold\\\\Tier\"") != NULL, "query_server escapes backslashes in rank");
    CHECK(output && strstr(output, "\"player\":{\"id\":1") != NULL, "query_server search returns a valid player object");
    CHECK(output && strstr(output, "\"player\":{{") == NULL, "query_server search avoids double object braces");
    CHECK(output && strstr(output, "\"nickname\":\"A\"B\"") == NULL, "query_server avoids raw quote breakage");

    free(output);
    remove(csv_path);
    remove(output_path);
}

static void test_bench_json_escaping(void) {
    char csv_path[128];
    char json_path[128];
    char stdout_path[128];
    char command[384];
    char *output = NULL;
    int wrote_fixture;
    int ran_command = 0;

    printf("\n[bench JSON escaping]\n");
    make_temp_path(csv_path, sizeof(csv_path), "bench_fixture.csv");
    make_temp_path(json_path, sizeof(json_path), "bench_results.json");
    make_temp_path(stdout_path, sizeof(stdout_path), "bench_stdout.log");

    wrote_fixture = write_escape_fixture_csv(csv_path);
    if (wrote_fixture) {
        snprintf(
            command,
            sizeof(command),
            "./bin/bench %s %s > %s",
            csv_path,
            json_path,
            stdout_path
        );
        ran_command = run_command_ok(command);
        if (ran_command) {
            output = read_text_file(json_path);
        }
    }

    CHECK(wrote_fixture, "fixture csv created for bench");
    CHECK(ran_command, "bench runs against escaped fixture");
    CHECK(output && strstr(output, "\"nickname\": \"Slash\\\\Name\"") != NULL, "bench escapes backslashes in top player nickname");
    CHECK(output && strstr(output, "\"rank\": \"Dia\\\"mond\"") != NULL, "bench escapes quotes in top player rank");
    CHECK(output && strstr(output, "\"rank\": \"Dia\"mond\"") == NULL, "bench avoids raw quote breakage");

    free(output);
    remove(csv_path);
    remove(json_path);
    remove(stdout_path);
}

static void test_query_server_top10_realtime(void) {
    char csv_path[128];
    char output_path[128];
    char command[384];
    char *output = NULL;
    char *lambda_pos;
    char *delta_pos;
    int wrote_fixture;
    int ran_command = 0;

    printf("\n[query_server TOP10 realtime]\n");
    make_temp_path(csv_path, sizeof(csv_path), "query_server_top_fixture.csv");
    make_temp_path(output_path, sizeof(output_path), "query_server_top_output.log");

    wrote_fixture = write_top_fixture_csv(csv_path);
    if (wrote_fixture) {
        snprintf(
            command,
            sizeof(command),
            "printf 'top\\nquit\\n' | ./bin/query_server %s > %s",
            csv_path,
            output_path
        );
        ran_command = run_command_ok(command);
        if (ran_command) {
            output = read_text_file(output_path);
        }
    }

    lambda_pos = output ? strstr(output, "\"nickname\":\"Lambda\"") : NULL;
    delta_pos = output ? strstr(output, "\"nickname\":\"Delta\"") : NULL;

    CHECK(wrote_fixture, "fixture csv created for query_server top10");
    CHECK(ran_command, "query_server top10 command runs against fixture");
    CHECK(output && strstr(output, "\"top_count\":10") != NULL, "query_server top10 reports 10 ranked players");
    CHECK(output && strstr(output, "\"linear_time\":") != NULL, "query_server top10 includes realtime timings");
    CHECK(lambda_pos && delta_pos && lambda_pos < delta_pos, "query_server top10 keeps highest win rate first");

    free(output);
    remove(csv_path);
    remove(output_path);
}

static void test_query_server_range_pagination(void) {
    char csv_path[128];
    char output_path[128];
    char command[384];
    char *output = NULL;
    char *first_item;
    char *last_item;
    int wrote_fixture;
    int ran_command = 0;

    printf("\n[query_server range pagination]\n");
    make_temp_path(csv_path, sizeof(csv_path), "query_server_range_fixture.csv");
    make_temp_path(output_path, sizeof(output_path), "query_server_range_output.log");

    wrote_fixture = write_range_fixture_csv(csv_path);
    if (wrote_fixture) {
        snprintf(
            command,
            sizeof(command),
            "printf 'range 5 24 2 10\\nquit\\n' | ./bin/query_server %s > %s",
            csv_path,
            output_path
        );
        ran_command = run_command_ok(command);
        if (ran_command) {
            output = read_text_file(output_path);
        }
    }

    first_item = output ? strstr(output, "\"id\":15") : NULL;
    last_item = output ? strstr(output, "\"id\":24") : NULL;

    CHECK(wrote_fixture, "fixture csv created for query_server range pagination");
    CHECK(ran_command, "query_server range pagination command runs against fixture");
    CHECK(output && strstr(output, "\"range_count\":20") != NULL, "query_server range pagination reports total match count");
    CHECK(output && strstr(output, "\"page\":2") != NULL, "query_server range pagination reports current page");
    CHECK(output && strstr(output, "\"page_count\":2") != NULL, "query_server range pagination reports total pages");
    CHECK(first_item && last_item && first_item < last_item, "query_server range pagination returns second page rows");
    CHECK(output && strstr(output, "\"id\":5") == NULL, "query_server range pagination excludes first page rows");

    free(output);
    remove(csv_path);
    remove(output_path);
}

static void test_bptree_single_search(void) {
    Player players[3];
    BPTree *tree = bptree_create();

    printf("\n[B+ Tree single search]\n");
    fill_players(players, 3);
    players[0].id = 10;
    players[1].id = 20;
    players[2].id = 30;

    bptree_insert(tree, players[0].id, &players[0]);
    bptree_insert(tree, players[1].id, &players[1]);
    bptree_insert(tree, players[2].id, &players[2]);

    CHECK(((Player *)bptree_search(tree, 10))->id == 10, "search finds id=10");
    CHECK(((Player *)bptree_search(tree, 20))->id == 20, "search finds id=20");
    CHECK(((Player *)bptree_search(tree, 30))->id == 30, "search finds id=30");
    CHECK(bptree_search(tree, 99) == NULL, "missing id returns NULL");

    bptree_free(tree);
}

static void test_bptree_range_search(void) {
    Player players[10];
    BPTree *tree = bptree_create();
    int i;

    printf("\n[B+ Tree range search]\n");
    fill_players(players, 10);

    for (i = 0; i < 10; ++i) {
        bptree_insert(tree, players[i].id, &players[i]);
    }

    CHECK(bptree_range(tree, 3, 7) == 5, "range 3..7 returns 5 rows");
    CHECK(bptree_range(tree, 1, 10) == 10, "range 1..10 returns 10 rows");
    CHECK(bptree_range(tree, 50, 60) == 0, "range outside keys returns 0");

    bptree_free(tree);
}

static void test_bptree_split_handling(void) {
    Player *players = (Player *)calloc(BP_ORDER * 3, sizeof(Player));
    BPTree *tree = bptree_create();
    int i;
    int ok = 1;

    printf("\n[B+ Tree split handling]\n");
    fill_players(players, BP_ORDER * 3);

    for (i = 0; i < BP_ORDER * 3; ++i) {
        bptree_insert(tree, players[i].id, &players[i]);
    }

    for (i = 0; i < BP_ORDER * 3; ++i) {
        Player *found = (Player *)bptree_search(tree, players[i].id);
        if (!found || found->id != players[i].id) {
            printf("    mismatch at id=%d\n", players[i].id);
            ok = 0;
            break;
        }
    }

    CHECK(ok, "split keeps all keys searchable");

    bptree_free(tree);
    free(players);
}

int main(void) {
    printf("==============================\n");
    printf("  Tree Unit Tests\n");
    printf("==============================\n");

    test_btree_single_search();
    test_btree_range_search();
    test_btree_split_handling();
    test_btree_range_after_split();
    test_btree_duplicate_key_update_after_split();
    test_query_demo_json_escaping();
    test_query_server_json_escaping();
    test_bench_json_escaping();
    test_query_server_top10_realtime();
    test_query_server_range_pagination();
    test_bptree_single_search();
    test_bptree_range_search();
    test_bptree_split_handling();

    printf("\n==============================\n");
    printf("  Result: %d passed / %d failed\n", tests_passed, tests_failed);
    printf("==============================\n");

    return tests_failed == 0 ? 0 : 1;
}
