#include "foundation/cache_sweep.h"
#include "foundation/compat.h"
#include "foundation/compat_fs.h"
#include "foundation/constants.h"
#include "foundation/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Entry mtime in whole seconds. Accepts regular files and directories (the
 * search scratch is a directory). */
static bool sweep_entry_age_s(const char *path, int64_t *out) {
    cbm_path_info_t info;
    if (cbm_path_info_utf8(path, &info) != CBM_PATH_INFO_OK ||
        (!info.is_regular && !info.is_directory)) {
        return false;
    }
    *out = info.mtime_ns / 1000000000;
    return true;
}

/* "<project>-<epoch>.log": the trailing dash-digits group is the epoch, the
 * project itself may contain dashes and digits ("foo-42-1699.log" -> "foo-42").
 * Returns the project prefix length, 0 when the name is not a skip log. */
static size_t skip_log_project_len(const char *name) {
    size_t len = strlen(name);
    if (len < 6 || strncmp(name + len - 4, ".log", 4) != 0) {
        return 0;
    }
    size_t body = len - 4;
    size_t i = body;
    while (i > 0 && name[i - 1] >= '0' && name[i - 1] <= '9') {
        i--;
    }
    /* A dash must separate a non-empty project from non-empty digits. */
    if (i < 2 || i == body || name[i - 1] != '-') {
        return 0;
    }
    return i - 1;
}

typedef struct {
    char *path;
    char *project;
    int64_t mtime_s;
} skip_log_entry_t;

static int skip_log_cmp(const void *a, const void *b) {
    const skip_log_entry_t *ea = (const skip_log_entry_t *)a;
    const skip_log_entry_t *eb = (const skip_log_entry_t *)b;
    int c = strcmp(ea->project, eb->project);
    if (c != 0) {
        return c;
    }
    if (ea->mtime_s != eb->mtime_s) {
        return ea->mtime_s > eb->mtime_s ? -1 : 1; /* newest first */
    }
    return strcmp(ea->path, eb->path);
}

static void skip_log_entries_free(skip_log_entry_t *entries, int count) {
    for (int i = 0; i < count; i++) {
        free(entries[i].path);
        free(entries[i].project);
    }
    free(entries);
}

int cbm_cache_sweep_skip_logs(const char *cache_dir) {
    if (!cache_dir || !cache_dir[0]) {
        return -1;
    }
    char logs_dir[CBM_SZ_1K];
    int written = snprintf(logs_dir, sizeof(logs_dir), "%s/logs", cache_dir);
    if (written <= 0 || written >= (int)sizeof(logs_dir)) {
        return -1;
    }
    cbm_dir_t *dir = cbm_opendir(logs_dir);
    if (!dir) {
        return -1;
    }
    skip_log_entry_t *entries = NULL;
    int count = 0;
    int cap = 0;
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        size_t project_len = skip_log_project_len(entry->name);
        if (project_len == 0) {
            continue;
        }
        size_t name_len = strlen(entry->name);
        char *path = (char *)malloc((size_t)written + 1 + name_len + 1);
        char *project = (char *)malloc(project_len + 1);
        if (!path || !project) {
            free(path);
            free(project);
            continue;
        }
        memcpy(path, logs_dir, (size_t)written);
        path[written] = '/';
        memcpy(path + written + 1, entry->name, name_len + 1);
        memcpy(project, entry->name, project_len);
        project[project_len] = '\0';
        int64_t mtime_s = 0;
        if (!sweep_entry_age_s(path, &mtime_s)) {
            free(path);
            free(project);
            continue;
        }
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            skip_log_entry_t *grown =
                (skip_log_entry_t *)realloc(entries, (size_t)cap * sizeof(*entries));
            if (!grown) {
                skip_log_entries_free(entries, count);
                free(path);
                free(project);
                cbm_closedir(dir);
                return -1;
            }
            entries = grown;
        }
        entries[count].path = path;
        entries[count].project = project;
        entries[count].mtime_s = mtime_s;
        count++;
    }
    cbm_closedir(dir);
    if (count == 0) {
        free(entries);
        return 0;
    }
    qsort(entries, (size_t)count, sizeof(*entries), skip_log_cmp);
    int64_t now_s = (int64_t)time(NULL);
    int64_t stale_before = now_s - CBM_CACHE_SWEEP_SKIP_LOGS_MAX_AGE_S;
    int removed = 0;
    const char *current_project = NULL;
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (!current_project || strcmp(current_project, entries[i].project) != 0) {
            current_project = entries[i].project;
            kept = 0;
        }
        bool too_old = entries[i].mtime_s < stale_before;
        if (kept < CBM_CACHE_SWEEP_SKIP_LOGS_KEEP && !too_old) {
            kept++;
            continue;
        }
        if (cbm_unlink(entries[i].path) == 0) {
            removed++;
        } else {
            char path_text[CBM_SZ_1K];
            snprintf(path_text, sizeof(path_text), "%s", entries[i].path);
            cbm_log_warn("cache.sweep.unlink_failed", "path", path_text);
        }
    }
    skip_log_entries_free(entries, count);
    return removed;
}

int cbm_cache_sweep_worker_temp(const char *cache_dir) {
    if (!cache_dir || !cache_dir[0]) {
        return -1;
    }
    char logs_dir[CBM_SZ_1K];
    int written = snprintf(logs_dir, sizeof(logs_dir), "%s/logs", cache_dir);
    if (written <= 0 || written >= (int)sizeof(logs_dir)) {
        return -1;
    }
    cbm_dir_t *dir = cbm_opendir(logs_dir);
    if (!dir) {
        return -1;
    }
    static const char prefix[] = ".worker-";
    int64_t now_s = (int64_t)time(NULL);
    int removed = 0;
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        if (strncmp(entry->name, prefix, sizeof(prefix) - 1) != 0) {
            continue;
        }
        char path[CBM_SZ_2K];
        int n = snprintf(path, sizeof(path), "%s/%s", logs_dir, entry->name);
        if (n <= 0 || n >= (int)sizeof(path)) {
            continue;
        }
        int64_t mtime_s = 0;
        if (!sweep_entry_age_s(path, &mtime_s)) {
            continue;
        }
        if (now_s - mtime_s < CBM_CACHE_SWEEP_TMP_MAX_AGE_S) {
            continue;
        }
        if (cbm_unlink(path) == 0) {
            removed++;
        }
    }
    cbm_closedir(dir);
    return removed;
}

int cbm_cache_sweep_search_scratch(void) {
    const char *tmp = cbm_tmpdir();
    if (!tmp || !tmp[0]) {
        return -1;
    }
    cbm_dir_t *dir = cbm_opendir(tmp);
    if (!dir) {
        return -1;
    }
    static const char prefix[] = "cbm-search-";
    int64_t now_s = (int64_t)time(NULL);
    int removed = 0;
    cbm_dirent_t *entry;
    while ((entry = cbm_readdir(dir)) != NULL) {
        if (strncmp(entry->name, prefix, sizeof(prefix) - 1) != 0) {
            continue;
        }
        char path[CBM_SZ_2K];
        int n = snprintf(path, sizeof(path), "%s/%s", tmp, entry->name);
        if (n <= 0 || n >= (int)sizeof(path)) {
            continue;
        }
        int64_t mtime_s = 0;
        if (!sweep_entry_age_s(path, &mtime_s)) {
            continue;
        }
        if (now_s - mtime_s < CBM_CACHE_SWEEP_TMP_MAX_AGE_S) {
            continue;
        }
        /* The scratch holds at most its two files (pattern, file list); remove
         * what unlinks cleanly, then the directory. A directory that still
         * resists rmdir stays for the next sweep — never a hard failure. */
        cbm_dir_t *scratch = cbm_opendir(path);
        if (scratch) {
            cbm_dirent_t *inner;
            while ((inner = cbm_readdir(scratch)) != NULL) {
                if (strcmp(inner->name, ".") == 0 || strcmp(inner->name, "..") == 0) {
                    continue;
                }
                char inner_path[CBM_SZ_2K];
                int m = snprintf(inner_path, sizeof(inner_path), "%s/%s", path, inner->name);
                if (m > 0 && m < (int)sizeof(inner_path)) {
                    (void)cbm_unlink(inner_path);
                }
            }
            cbm_closedir(scratch);
        }
        if (cbm_rmdir(path) == 0) {
            removed++;
        }
    }
    cbm_closedir(dir);
    return removed;
}

int cbm_cache_sweep_run(const char *cache_dir) {
    int removed = cbm_cache_sweep_skip_logs(cache_dir);
    int worker = cbm_cache_sweep_worker_temp(cache_dir);
    int scratch = cbm_cache_sweep_search_scratch();
    if (removed < 0 || worker < 0 || scratch < 0) {
        return -1;
    }
    return removed + worker + scratch;
}
