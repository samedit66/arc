#ifndef ARC_INTERNAL_LIBARCHIVE_ADAPTER_H
#define ARC_INTERNAL_LIBARCHIVE_ADAPTER_H

#include "arc.h"

typedef struct {
    unsigned mode;
    int mtime_set;
    int64_t mtime;
    long mtime_nsec;
} ArchiveMetadata;

#include <archive.h>
#include <archive_entry.h>
ArcResult backend_error(struct archive *a, ArcCode fallback);
ArcResult backend_configure_writer(struct archive *backend, const ArcOptions *options);
ArcResult entry_text(struct archive_entry *entry, int which, char **out);
ArcEntryType entry_type(struct archive_entry *entry);
void clean_metadata(struct archive_entry *entry);
ArchiveMetadata backend_file_metadata(struct archive_entry *entry);
ArcResult disk_open(struct archive *disk, const char *path);
ArcResult reader_open(const char *path, const char *password, struct archive **out);
ArcResult read_header(struct archive *a, struct archive_entry **entry, int *end);
ArcResult validate_password(const char *password);
ArcResult normalized_entry(struct archive *a, struct archive_entry *entry);
/**
 * Closes/frees the reader; an earlier error takes precedence over cleanup.
 *
 * - `a`: Backend reader to close and free.
 * - `r`: Earlier result; its failure takes precedence over cleanup.
 */
ArcResult reader_release(struct archive *a, ArcResult r);

#endif
