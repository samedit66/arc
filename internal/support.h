#ifndef ARC_INTERNAL_SUPPORT_H
#define ARC_INTERNAL_SUPPORT_H

#include "../arc.h"
#include "../filekit/filekit.h"
/**
 * Translate filekit errors at the archive API boundary, preserving diagnostics.
 *
 * - `result`: Filekit result to translate; preserve its diagnostic.
 */
ArcResult ar_from_filekit(FilekitResult result);
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
ArcResult ar_result(ArcCode code, const char *format, ...);
ArcResult ar_okay(void);
int ar_failed(ArcResult r);
/**
 * Retain primary code; append bounded secondary cleanup diagnostics.
 *
 * - `primary`: Original operation result whose error takes precedence.
 * - `cleanup`: Secondary cleanup result to append when it fails.
 */
ArcResult ar_cleanup(ArcResult primary, ArcResult cleanup);
ArcResult ar_system_error(const char *operation, const char *path);
ArcResult ar_copy_string(const char *s, char **out);
ArcResult ar_join(const char *a, const char *b, char **out);
int ar_valid_utf8(const char *s);

typedef struct {
    const char *suffix, *format, *compression;
} ArchiveSuffix;

/**
 * Longest ASCII case-insensitive suffix; creation uses the common writable formats.
 *
 * - `path`: Archive filename to examine.
 * - `creation`: Nonzero limits matching to common writable formats.
 */
const ArchiveSuffix *ar_suffix_of(const char *path, int creation);

#endif
