#ifdef _WIN32
#include "arc.h"
#include "filekit/filekit.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int fail_delete;

static int fail_descriptor(intptr_t handle, int flags) {
    (void)handle;
    (void)flags;
    errno = EMFILE;
    return -1;
}

static BOOL WINAPI delete_output(LPCWSTR path) {
    if (fail_delete) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return DeleteFileW(path);
}

/* Substitute only the failing native boundaries; creation and handle closing
 * remain real Win32 operations in the production filekit implementation. */
#define _open_osfhandle fail_descriptor
#define DeleteFileW delete_output
#include "filekit/file_system_windows.c"
#undef DeleteFileW
#undef _open_osfhandle

int main(void) {
    int descriptor = 123;
    FilekitResult f = filekit_create_file_exclusive("rolled-back", 0600, &descriptor);
    assert(f.code == FILEKIT_IO_ERROR && descriptor == 123);
    assert(GetFileAttributesW(L"rolled-back") == INVALID_FILE_ATTRIBUTES);
    ArcWriter *writer = NULL;
    ArcResult r = arc_create("rolled-back.tar", &writer);
    assert(r.code == ARC_IO_ERROR && !writer);
    assert(GetFileAttributesW(L"rolled-back.tar") == INVALID_FILE_ATTRIBUTES);
    fail_delete = 1;
    r = arc_create("leftover.tar", &writer);
    assert(r.code == ARC_IO_ERROR && !writer);
    assert(strstr(r.message, "Incomplete output remains"));
    assert(GetFileAttributesW(L"leftover.tar") != INVALID_FILE_ATTRIBUTES);
    fail_delete = 0;
    assert(DeleteFileW(L"leftover.tar"));
    FILE *existing = fopen("existing.tar", "wb");
    assert(existing && fputs("sentinel", existing) >= 0 && fclose(existing) == 0);
    r = arc_create("existing.tar", &writer);
    assert(r.code == ARC_ALREADY_EXISTS && !writer);
    existing = fopen("existing.tar", "rb");
    char data[16] = {0};
    assert(existing && fread(data, 1, sizeof data, existing) == 8);
    assert(!strcmp(data, "sentinel") && fclose(existing) == 0);
    puts("Windows descriptor failure rollback passed.");
    return 0;
}
#endif
