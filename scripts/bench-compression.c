/* Experimental adapter replacement; link before libarc.a, never into distributions. */
#define backend_configure_writer benchmark_default_writer
#include "../src/internal/libarchive_adapter.c"
#undef backend_configure_writer

ArcResult backend_configure_writer(struct archive *backend, const ArcOptions *options) {
    ArcResult r = benchmark_default_writer(backend, options);
    const char *level = getenv("ARC_BENCH_LEVEL");
    if (ar_failed(r) || !level)
        return r;
    if (strlen(level) != 1 || level[0] < '0' || level[0] > '9')
        return ar_result(ARC_INVALID_ARGUMENT, "Benchmark level must be 0 through 9.");
    int status;
    if (!strcmp(options->format, "zip")) {
        status = archive_write_set_format_option(backend, "zip", "compression", "deflate");
        if (status == ARCHIVE_OK)
            status = archive_write_set_format_option(backend, "zip", "compression-level", level);
    } else if (!strcmp(options->compression, "gzip"))
        status = archive_write_set_filter_option(backend, "gzip", "compression-level", level);
    else
        return ar_result(ARC_INVALID_ARGUMENT, "Compression probe requires gzip or ZIP.");
    return status == ARCHIVE_OK ? ar_okay() : backend_error(backend, ARC_IO_ERROR);
}
