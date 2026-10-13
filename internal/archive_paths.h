#ifndef ARC_INTERNAL_ARCHIVE_PATHS_H
#define ARC_INTERNAL_ARCHIVE_PATHS_H

#include "../arc.h"
ArcResult entry_name(const char *input, char **out);
/**
 * Match a normalized selector against raw archive text, without host validation.
 *
 * - `selector`: Normalized relative selection path.
 * - `candidate`: Raw stored archive path to match, including descendants.
 */
int entry_path_selected(const char *selector, const char *candidate);
/**
 * True for equal names, or a non-directory used as another entry's parent.
 *
 * - `a`: First archive name.
 * - `a_type`: First entry type.
 * - `b`: Second archive name.
 * - `b_type`: Second entry type.
 * - `conflict`: Output flag for equal names or non-directory parent conflicts.
 */
ArcResult entry_paths_conflict(const char *a, ArcEntryType a_type, const char *b,
                               ArcEntryType b_type, int *conflict);

typedef struct ArchivePathNode ArchivePathNode;

typedef struct {
    ArchivePathNode *root;
} ArchivePathIndex;

/**
 * Find the record associated with an archive name.
 *
 * - `index`: Path index to search.
 * - `name`: Archive name to look up.
 * - `record`: Output record number; SIZE_MAX when absent, unchanged on failure.
 */
ArcResult archive_paths_find(const ArchivePathIndex *index, const char *name, size_t *record);
/**
 * Reject duplicate names and non-directory prefix conflicts before insertion.
 *
 * - `index`: Path index to update.
 * - `name`: Archive name to copy into the index.
 * - `type`: Entry type used to detect parent conflicts.
 * - `record`: Caller record number to associate with the name.
 */
ArcResult archive_paths_insert(ArchivePathIndex *index, const char *name, ArcEntryType type,
                               size_t record);
void archive_paths_free(ArchivePathIndex *index);

#endif
