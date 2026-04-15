#include "bplus_tree.h"

#include <stdlib.h>

extern long long g_op_count;

typedef struct {
    int did_split;
    int promoted_key;
    BPNode *right;
} BPInsertResult;

static BPLeaf *new_leaf(void) {
    return (BPLeaf *)calloc(1, sizeof(BPLeaf));
}

static BPNode *new_node(int is_leaf) {
    BPNode *node = (BPNode *)calloc(1, sizeof(BPNode));
    if (!node) {
        return NULL;
    }

    node->is_leaf = is_leaf;
    if (is_leaf) {
        node->leaf = new_leaf();
        if (!node->leaf) {
            free(node);
            return NULL;
        }
    }

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

static BPInsertResult insert_recursive(BPNode *node, int key, void *record_ptr) {
    BPInsertResult result = {0, 0, NULL};
    int idx = node->is_leaf
        ? lower_bound_keys(node->keys, node->num_keys, key)
        : upper_bound_keys(node->keys, node->num_keys, key);

    if (node->is_leaf) {
        BPLeaf *leaf = node->leaf;
        int i;

        if (idx < leaf->num_keys && leaf->keys[idx] == key) {
            leaf->ptrs[idx] = record_ptr;
            return result;
        }

        for (i = leaf->num_keys; i > idx; --i) {
            leaf->keys[i] = leaf->keys[i - 1];
            leaf->ptrs[i] = leaf->ptrs[i - 1];
        }

        leaf->keys[idx] = key;
        leaf->ptrs[idx] = record_ptr;
        leaf->num_keys += 1;
        node->num_keys = leaf->num_keys;

        for (i = 0; i < leaf->num_keys; ++i) {
            node->keys[i] = leaf->keys[i];
        }

        if (leaf->num_keys <= BP_ORDER) {
            return result;
        }

        {
            BPNode *right = new_node(1);
            BPLeaf *right_leaf;
            int total = leaf->num_keys;
            int left_count = total / 2;
            int right_count = total - left_count;

            if (!right) {
                return result;
            }

            right_leaf = right->leaf;
            for (i = 0; i < right_count; ++i) {
                right_leaf->keys[i] = leaf->keys[left_count + i];
                right_leaf->ptrs[i] = leaf->ptrs[left_count + i];
                right->keys[i] = right_leaf->keys[i];
            }

            right_leaf->num_keys = right_count;
            right_leaf->next = leaf->next;
            right->num_keys = right_count;

            leaf->num_keys = left_count;
            leaf->next = right_leaf;
            node->num_keys = left_count;
            for (i = 0; i < left_count; ++i) {
                node->keys[i] = leaf->keys[i];
            }

            result.did_split = 1;
            result.promoted_key = right_leaf->keys[0];
            result.right = right;
            return result;
        }
    }

    {
        BPInsertResult child_result = insert_recursive(node->children[idx], key, record_ptr);
        int i;

        if (!child_result.did_split) {
            return result;
        }

        for (i = node->num_keys; i > idx; --i) {
            node->keys[i] = node->keys[i - 1];
        }
        for (i = node->num_keys + 1; i > idx + 1; --i) {
            node->children[i] = node->children[i - 1];
        }

        node->keys[idx] = child_result.promoted_key;
        node->children[idx + 1] = child_result.right;
        node->num_keys += 1;

        if (node->num_keys <= BP_ORDER) {
            return result;
        }

        {
            BPNode *right = new_node(0);
            int total = node->num_keys;
            int mid = total / 2;
            int promoted_key = node->keys[mid];
            int right_keys = total - mid - 1;

            if (!right) {
                return result;
            }

            for (i = 0; i < right_keys; ++i) {
                right->keys[i] = node->keys[mid + 1 + i];
            }
            for (i = 0; i < right_keys + 1; ++i) {
                right->children[i] = node->children[mid + 1 + i];
            }

            right->num_keys = right_keys;
            node->num_keys = mid;

            result.did_split = 1;
            result.promoted_key = promoted_key;
            result.right = right;
            return result;
        }
    }
}

static BPLeaf *find_leaf_node(BPNode *node, int key) {
    BPNode *current = node;

    while (current && !current->is_leaf) {
        int idx = 0;
        while (idx < current->num_keys && current->keys[idx] <= key) {
            g_op_count++;
            idx++;
        }
        if (idx < current->num_keys) {
            g_op_count++;
        }
        current = current->children[idx];
    }

    return current ? current->leaf : NULL;
}

static void free_recursive(BPNode *node) {
    int i;

    if (!node) {
        return;
    }

    if (node->is_leaf) {
        free(node->leaf);
    } else {
        for (i = 0; i <= node->num_keys; ++i) {
            free_recursive(node->children[i]);
        }
    }

    free(node);
}

BPTree *bptree_create(void) {
    BPTree *tree = (BPTree *)calloc(1, sizeof(BPTree));
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

void bptree_insert(BPTree *tree, int key, void *record_ptr) {
    BPInsertResult result;
    BPNode *new_root;

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
    new_root->children[0] = tree->root;
    new_root->children[1] = result.right;
    new_root->num_keys = 1;
    tree->root = new_root;
}

void *bptree_search(BPTree *tree, int key) {
    BPLeaf *leaf;
    int i;

    if (!tree || !tree->root) {
        return NULL;
    }

    leaf = find_leaf_node(tree->root, key);
    if (!leaf) {
        return NULL;
    }

    for (i = 0; i < leaf->num_keys; ++i) {
        g_op_count++;
        if (leaf->keys[i] == key) {
            return leaf->ptrs[i];
        }
        if (leaf->keys[i] > key) {
            return NULL;
        }
    }

    return NULL;
}

int bptree_range(BPTree *tree, int lo, int hi) {
    BPLeaf *leaf;
    int count = 0;

    if (!tree || !tree->root || lo > hi) {
        return 0;
    }

    leaf = find_leaf_node(tree->root, lo);
    while (leaf) {
        int i;

        for (i = 0; i < leaf->num_keys; ++i) {
            g_op_count++;
            if (leaf->keys[i] < lo) {
                continue;
            }
            if (leaf->keys[i] > hi) {
                return count;
            }
            count += 1;
        }

        leaf = leaf->next;
    }

    return count;
}

void bptree_free(BPTree *tree) {
    if (!tree) {
        return;
    }

    free_recursive(tree->root);
    free(tree);
}
