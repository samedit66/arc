#ifndef CLI_PROGRESS_H
#define CLI_PROGRESS_H
#include "arc.h"
/* CLI-owned synchronous progress display. Stop before other diagnostics. */
typedef struct Progress Progress;
Progress *progress_start(const char *action, const char *path);
void progress_update(const ArcProgress *snapshot, void *context);
ArcObserver progress_observer(Progress *progress);
void progress_stop(Progress *progress);
#endif
