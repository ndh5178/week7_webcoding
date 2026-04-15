#include "linear_search.h"
#include <string.h>
#include <stddef.h>

/* benchmark.c에서 정의된 전역 연산 횟수 카운터 */
extern long long g_op_count;

Player *linear_search(Player *table, int size, int target_id) {
    for (int i = 0; i < size; i++) {
        g_op_count++;                      /* 비교 1회 */
        if (table[i].id == target_id)
            return &table[i];
    }
    return NULL;
}

Player *linear_search_name(Player *table, int size, const char *target_name) {
    if (!target_name) {
        return NULL;
    }

    for (int i = 0; i < size; i++) {
        g_op_count++;                      /* 문자열 비교 1회 */
        if (strcmp(table[i].name, target_name) == 0)
            return &table[i];
    }
    return NULL;
}

int linear_range(Player *table, int size, int lo, int hi) {
    int count = 0;
    for (int i = 0; i < size; i++) {
        g_op_count++;                      /* 비교 1회 */
        if (table[i].id >= lo && table[i].id <= hi)
            count++;
    }
    return count;
}
