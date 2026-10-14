#ifndef CLI_PROGRESS_H
#define CLI_PROGRESS_H
/* CLI-owned activity display. Stop before printing any other diagnostics. */
typedef struct Progress Progress;
Progress *progress_start(const char *action, const char *path);
void progress_stop(Progress *progress);
#endif
