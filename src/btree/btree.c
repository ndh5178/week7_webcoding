#include "btree.h"

#include <stdlib.h>
#include <string.h>

/* Default comparison counter used when no stronger definition is linked. */
__attribute__((weak)) long long g_op_count = 0;

typedef struct {
    int did_split;
    int promoted_key;
    void *promoted_ptr;
    BTNode *right;
} BTInsertResult;

static BTNode *new_node(int is_leaf) {
    BTNode *node = (BTNode *)calloc(1, sizeof(BTNode));
    if (!node) {
        return NULL;
    }

    node->is_leaf = is_leaf;
    return node;
}

static int lower_bound_keys(const int *keys, int count, int key) {
    int lo = 0;
    int hi = count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (keys[mid] < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

static int upper_bound_keys(const int *keys, int count, int key) {
    int lo = 0;
    int hi = count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (keys[mid] <= key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

static BTInsertResult insert_recursive(BTNode *node, int key, void *record_ptr) {
    BTInsertResult result = {0, 0, NULL, NULL};
    int idx = node->is_leaf
        ? lower_bound_keys(node->keys, node->num_keys, key)
        : upper_bound_keys(node->keys, node->num_keys, key);

    if (node->is_leaf) {
        int i;

        if (idx < node->num_keys && node->keys[idx] == key) {
            node->ptrs[idx] = record_ptr;
            return result;
        }

        for (i = node->num_keys; i > idx; --i) {
            node->keys[i] = node->keys[i - 1];
            node->ptrs[i] = node->ptrs[i - 1];
        }

        node->keys[idx] = key;
        node->ptrs[idx] = record_ptr;
        node->num_keys += 1;

        if (node->num_keys <= BT_ORDER) {
            return result;
        }
    } else {
        if (idx > 0 && node->keys[idx - 1] == key) {
            node->ptrs[idx - 1] = record_ptr;
        }

        {
            BTInsertResult child_result = insert_recursive(node->children[idx], key, record_ptr);
            int i;

            if (!child_result.did_split) {
                return result;
            }

            for (i = node->num_keys; i > idx; --i) {
                node->keys[i] = node->keys[i - 1];
                node->ptrs[i] = node->ptrs[i - 1];
            }
            for (i = node->num_keys + 1; i > idx + 1; --i) {
                node->children[i] = node->children[i - 1];
            }

            node->keys[idx] = child_result.promoted_key;
            node->ptrs[idx] = child_result.promoted_ptr;
            node->children[idx + 1] = child_result.right;
            node->num_keys += 1;

            if (node->num_keys <= BT_ORDER) {
                return result;
            }
        }
    }

    {
        BTNode *right = new_node(node->is_leaf);
        int total = node->num_keys;
        int mid = total / 2;
        int right_keys;
        int i;

        result.did_split = 1;
        result.right = right;

        if (node->is_leaf) {
            right_keys = total - mid;

            for (i = 0; i < right_keys; ++i) {
                right->keys[i] = node->keys[mid + i];
                right->ptrs[i] = node->ptrs[mid + i];
            }

            right->num_keys = right_keys;
            node->num_keys = mid;
            result.promoted_key = right->keys[0];
            result.promoted_ptr = right->ptrs[0];
        } else {
            int promoted_index = mid;
            int right_children = total - promoted_index - 1;

            result.promoted_key = node->keys[promoted_index];
            result.promoted_ptr = node->ptrs[promoted_index];

            for (i = 0; i < right_children; ++i) {
                right->keys[i] = node->keys[promoted_index + 1 + i];
                right->ptrs[i] = node->ptrs[promoted_index + 1 + i];
            }
            for (i = 0; i < right_children + 1; ++i) {
                right->children[i] = node->children[promoted_index + 1 + i];
            }

            right->num_keys = right_children;
            node->num_keys = promoted_index;
        }
    }

    return result;
}

static void *search_recursive(BTNode *node, int key) {
    int idx;

    if (!node) {
        return NULL;
    }

    idx = 0;
    if (node->is_leaf) {
        while (idx < node->num_keys && node->keys[idx] < key) {
            g_op_count++;
            idx++;
        }
        if (idx < node->num_keys) {
            g_op_count++;
        }

        if (idx < node->num_keys && node->keys[idx] == key) {
            return node->ptrs[idx];
        }
        return NULL;
    }

    while (idx < node->num_keys && node->keys[idx] <= key) {
        g_op_count++;
        idx++;
    }
    if (idx < node->num_keys) {
        g_op_count++;
    }

    return search_recursive(node->children[idx], key);
}

static int range_recursive(BTNode *node, int lo, int hi) {
    int count = 0;
    int i;

    if (!node) {
        return 0;
    }

    if (node->is_leaf) {
        for (i = 0; i < node->num_keys; ++i) {
            g_op_count++;
            if (node->keys[i] >= lo && node->keys[i] <= hi) {
                count += 1;
            }
        }
        return count;
    }

    i = 0;
    while (i < node->num_keys && node->keys[i] <= lo) {
        g_op_count++;
        i++;
    }
    if (i < node->num_keys) {
        g_op_count++;
    }

    count += range_recursive(node->children[i], lo, hi);

    while (i < node->num_keys && node->keys[i] <= hi) {
        g_op_count++;
        i++;
        count += range_recursive(node->children[i], lo, hi);
    }
    if (i < node->num_keys) {
        g_op_count++;
    }

    return count;
}

static void free_recursive(BTNode *node) {
    int i;

    if (!node) {
        return;
    }

    if (!node->is_leaf) {
        for (i = 0; i <= node->num_keys; ++i) {
            free_recursive(node->children[i]);
        }
    }

    free(node);
}

BTree *btree_create(void) {
    BTree *tree = (BTree *)calloc(1, sizeof(BTree));
    if (!tree) {
        return NULL;
    }

    tree->root = new_node(1);
    if (!tree->root) {
        free(tree);
        return NULL;
    }

    return tree;
}

void btree_insert(BTree *tree, int key, void *record_ptr) {
    BTInsertResult result;
    BTNode *new_root;

    if (!tree || !tree->root) {
        return;
    }

    result = insert_recursive(tree->root, key, record_ptr);
    if (!result.did_split) {
        return;
    }

    new_root = new_node(0);
    if (!new_root) {
        return;
    }

    new_root->keys[0] = result.promoted_key;
    new_root->ptrs[0] = result.promoted_ptr;
    new_root->children[0] = tree->root;
    new_root->children[1] = result.right;
    new_root->num_keys = 1;
    tree->root = new_root;
}

void *btree_search(BTree *tree, int key) {
    if (!tree) {
        return NULL;
    }

    return search_recursive(tree->root, key);
}

int btree_range(BTree *tree, int lo, int hi) {
    if (!tree || lo > hi) {
        return 0;
    }

    return range_recursive(tree->root, lo, hi);
}

void btree_free(BTree *tree) {
    if (!tree) {
        return;
    }

    free_recursive(tree->root);
    free(tree);
}
