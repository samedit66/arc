#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "support.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "archive_paths.h"
#include "platform_text.h"

ArcResult entry_name(const char *input, char **out) {
    if (!input || !ar_valid_utf8(input))
        return ar_result(ARC_INVALID_PATH, "Invalid UTF-8 entry name.");
    while (input[0] == '.' && input[1] == '/')
        input += 2;
    if (!*input || *input == '/')
        return ar_diagnostic(ar_result(ARC_INVALID_PATH, "Entry path must be relative."),
                             ARC_REASON_UNSAFE_PATH, input);
    char *name = NULL;
    ArcResult r = ar_copy_string(input, &name);
    if (ar_failed(r))
        return r;
    size_t n = strlen(name);
    while (n && name[n - 1] == '/')
        name[--n] = 0;
    char *p = name;
    while (*p) {
        char *end = strchr(p, '/');
        size_t k = end ? (size_t)(end - p) : strlen(p);
        if (!k || (k == 1 && p[0] == '.') || (k == 2 && p[0] == '.' && p[1] == '.'))
            goto invalid;
        if (!platform_valid_component(p, k))
            goto invalid;
        if (!end)
            break;
        p = end + 1;
    }
    if (!n)
        goto invalid;
    *out = name;
    return ar_okay();
invalid:
    free(name);
    return ar_diagnostic(ar_result(ARC_INVALID_PATH, "Unsafe or invalid archive path."),
                         ARC_REASON_UNSAFE_PATH, input);
}

ArcResult entry_paths_conflict(const char *a, ArcEntryType a_type, const char *b,
                               ArcEntryType b_type, int *conflict) {
    *conflict = 0;
    ArcResult r = platform_compare_names(a, b, 0, conflict);
    if (!ar_failed(r) && !*conflict && a_type != ARC_ENTRY_DIRECTORY)
        r = platform_compare_names(a, b, 1, conflict);
    if (!ar_failed(r) && !*conflict && b_type != ARC_ENTRY_DIRECTORY)
        r = platform_compare_names(b, a, 1, conflict);
    return r;
}

int entry_path_selected(const char *selector, const char *candidate) {
    while (candidate[0] == '.' && candidate[1] == '/')
        candidate += 2;
    size_t n = strlen(selector), length = strlen(candidate);
    while (length && candidate[length - 1] == '/')
        --length;
    return length >= n && !strncmp(selector, candidate, n) && (length == n || candidate[n] == '/');
}

struct ArchivePathNode {
    char *name;
    ArcEntryType type;
    size_t record;
    int height;
    struct ArchivePathNode *left, *right;
};

static int path_height(const ArchivePathNode *node) {
    return node ? node->height : 0;
}

static void path_height_update(ArchivePathNode *node) {
    int left = path_height(node->left), right = path_height(node->right);
    node->height = 1 + (left > right ? left : right);
}

static ArchivePathNode *path_rotate(ArchivePathNode *node, int left) {
    ArchivePathNode *top = left ? node->right : node->left;
    if (left) {
        node->right = top->left;
        top->left = node;
    } else {
        node->left = top->right;
        top->right = node;
    }
    path_height_update(node);
    path_height_update(top);
    return top;
}

static ArchivePathNode *path_balance(ArchivePathNode *node) {
    path_height_update(node);
    int balance = path_height(node->left) - path_height(node->right);
    if (balance > 1) {
        if (path_height(node->left->left) < path_height(node->left->right))
            node->left = path_rotate(node->left, 1);
        return path_rotate(node, 0);
    }
    if (balance < -1) {
        if (path_height(node->right->right) < path_height(node->right->left))
            node->right = path_rotate(node->right, 0);
        return path_rotate(node, 1);
    }
    return node;
}

/* Recursion is bounded by AVL height, not archive depth or insertion order. */
static ArcResult path_insert_node(ArchivePathNode **root, ArchivePathNode *node) {
    if (!*root) {
        *root = node;
        return ar_okay();
    }
    int order;
    ArcResult r = platform_order_names(node->name, (*root)->name, &order);
    if (ar_failed(r))
        return r;
    r = path_insert_node(order < 0 ? &(*root)->left : &(*root)->right, node);
    if (!ar_failed(r))
        *root = path_balance(*root);
    return r;
}

static ArcResult path_lookup(const ArchivePathIndex *index, const char *name, int lower_bound,
                             const ArchivePathNode **out) {
    const ArchivePathNode *node = index->root, *found = NULL;
    while (node) {
        int order;
        ArcResult r = platform_order_names(name, node->name, &order);
        if (ar_failed(r))
            return r;
        if (!order) {
            found = node;
            break;
        }
        if (order < 0) {
            if (lower_bound)
                found = node;
            node = node->left;
        } else
            node = node->right;
    }
    *out = found;
    return ar_okay();
}

ArcResult archive_paths_find(const ArchivePathIndex *index, const char *name, size_t *record) {
    const ArchivePathNode *found = NULL;
    ArcResult r = path_lookup(index, name, 0, &found);
    if (!ar_failed(r))
        *record = found ? found->record : SIZE_MAX;
    return r;
}

ArcResult archive_paths_insert(ArchivePathIndex *index, const char *name, ArcEntryType type,
                               size_t record) {
    const ArchivePathNode *found = NULL;
    ArcResult r = path_lookup(index, name, 0, &found);
    if (ar_failed(r))
        return r;
    if (found)
        return ar_diagnostic(ar_result(ARC_ALREADY_EXISTS, "Conflicting archive entries."),
                             ARC_REASON_ARCHIVE_CONFLICT, name);
    char *prefix = NULL;
    r = ar_copy_string(name, &prefix);
    if (ar_failed(r))
        return r;
    for (char *p = prefix; *p && !ar_failed(r); ++p) {
        if (*p != '/')
            continue;
        *p = 0;
        r = path_lookup(index, prefix, 0, &found);
        *p = '/';
        if (!ar_failed(r) && found && found->type != ARC_ENTRY_DIRECTORY)
            r = ar_diagnostic(ar_result(ARC_ALREADY_EXISTS, "Conflicting archive entries."),
                              ARC_REASON_ARCHIVE_CONFLICT, name);
    }
    free(prefix);
    if (!ar_failed(r) && type != ARC_ENTRY_DIRECTORY) {
        char *descendants = NULL;
        size_t n = strlen(name);
        if (n > SIZE_MAX - 2)
            r = ar_result(ARC_OVERFLOW, "Path is too long.");
        else {
            descendants = malloc(n + 2);
            if (!descendants)
                r = ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate path.");
            else {
                memcpy(descendants, name, n);
                descendants[n] = '/';
                descendants[n + 1] = 0;
            }
        }
        if (!ar_failed(r))
            r = path_lookup(index, descendants, 1, &found);
        int conflict = 0;
        if (!ar_failed(r) && found)
            r = platform_compare_names(name, found->name, 1, &conflict);
        if (!ar_failed(r) && conflict)
            r = ar_diagnostic(ar_result(ARC_ALREADY_EXISTS, "Conflicting archive entries."),
                              ARC_REASON_ARCHIVE_CONFLICT, name);
        free(descendants);
    }
    if (ar_failed(r))
        return r;
    ArchivePathNode *node = calloc(1, sizeof *node);
    if (!node)
        return ar_result(ARC_OUT_OF_MEMORY, "Cannot allocate path index.");
    r = ar_copy_string(name, &node->name);
    node->type = type;
    node->record = record;
    node->height = 1;
    if (!ar_failed(r))
        r = path_insert_node(&index->root, node);
    if (ar_failed(r)) {
        free(node->name);
        free(node);
    }
    return r;
}

static void path_free_node(ArchivePathNode *node) {
    if (!node)
        return;
    path_free_node(node->left);
    path_free_node(node->right);
    free(node->name);
    free(node);
}

void archive_paths_free(ArchivePathIndex *index) {
    path_free_node(index->root);
    index->root = NULL;
}
