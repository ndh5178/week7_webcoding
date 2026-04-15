#include "linear_search.h"
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

int linear_range(Player *table, int size, int lo, int hi) {
    int count = 0;
    for (int i = 0; i < size; i++) {
        g_op_count++;                      /* 비교 1회 */
        if (table[i].id >= lo && table[i].id <= hi)
            count++;
    }
    return count;
}
