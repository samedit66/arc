#ifndef ARC_INTERNAL_ARC_INTERNAL_H
#define ARC_INTERNAL_ARC_INTERNAL_H

#include "../arc.h"
ArcResult arc_get_capabilities_impl(const ArcOptions *options, unsigned *out);
/**
 * Return the first retained writer failure. Public wrappers own the locale scope.
 *
 * - `writer`: Writer whose retained failure is returned.
 */
ArcResult writer_previous_failure(const Arc *writer);
ArcResult writer_failure(Arc *writer, ArcResult r);
ArcResult release_writer(Arc **writer, int discard);
ArcResult arc_create_impl(const char *path, const ArcOptions *options, Arc **out);
ArcResult arc_begin_file_impl(Arc *writer, const char *path, uint64_t size);
ArcResult arc_write_chunk_impl(Arc *writer, const void *data, size_t size);
ArcResult arc_end_file_impl(Arc *writer);
ArcResult arc_add_text_impl(Arc *writer, const char *path, const char *text);
ArcResult arc_add_file_impl(Arc *writer, const char *source, const char *name);
ArcResult arc_add_path_impl(Arc *writer, const char *source, const char *name);
ArcResult arc_list_impl(const char *path, const ArcReadOptions *options, ArcVisitor visitor,
                        void *context);
ArcResult arc_get_size_impl(const char *path, uint64_t *out);
ArcResult arc_inspect_impl(const char *path, const ArcReadOptions *options, ArcInfo *out);
ArcResult arc_unpack_impl(const char *path, const char *root, const ArcExtractOptions *options);
ArcResult arc_verify_impl(const char *path, const ArcReadOptions *options);

ArcResult sequential_reader_ready(ArcReader *reader, int require_entry);
ArcResult sequential_reader_failure(ArcReader *reader, ArcResult r);
ArcResult arc_reader_open_impl(const char *path, const ArcReadOptions *options, ArcReader **out);
ArcResult arc_reader_next_impl(ArcReader *reader, ArcEntry *entry, int *end);
ArcResult arc_reader_read_impl(ArcReader *reader, void *buffer, size_t capacity, size_t *received);
ArcResult arc_reader_skip_impl(ArcReader *reader);
ArcResult arc_reader_close_impl(ArcReader **reader);

#endif
