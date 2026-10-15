#include "progress.h"
#include "support.h"

static void publish(ProgressState *state) {
    state->observer.callback(&state->snapshot, state->observer.context);
}

void ar_progress_phase(ProgressState *state, ArcProgressPhase phase) {
    if (!state || !state->observer.callback)
        return;
    state->snapshot.phase = phase;
    state->snapshot.entries_seen = 0;
    state->snapshot.path = NULL;
    state->snapshot.entry_bytes = state->snapshot.entry_size = 0;
    state->snapshot.entry_size_known = 0;
    publish(state);
}

ArcResult ar_progress_entry(ProgressState *state, const char *path, uint64_t size, int known) {
    if (!state || !state->observer.callback)
        return ar_okay();
    if (state->snapshot.entries_seen == UINT64_MAX)
        return ar_result(ARC_OVERFLOW, "Progress entry count overflow.");
    ++state->snapshot.entries_seen;
    state->snapshot.path = path;
    state->snapshot.entry_bytes = 0;
    state->snapshot.entry_size = size;
    state->snapshot.entry_size_known = known;
    publish(state);
    return ar_okay();
}

ArcResult ar_progress_bytes(ProgressState *state, uint64_t bytes) {
    if (!state || !state->observer.callback || !bytes)
        return ar_okay();
    if (UINT64_MAX - state->snapshot.content_bytes < bytes ||
        UINT64_MAX - state->snapshot.entry_bytes < bytes)
        return ar_result(ARC_OVERFLOW, "Progress content size overflow.");
    state->snapshot.content_bytes += bytes;
    state->snapshot.entry_bytes += bytes;
    publish(state);
    return ar_okay();
}
