#ifndef CBM_CACHE_SWEEP_H
#define CBM_CACHE_SWEEP_H

/* Cache-directory housekeeping (fork issue #8). The cache only ever grew:
 * degraded-index skip logs accumulated one file per run
 * (<cache>/logs/<project>-<epoch>.log), crashed index supervisors left
 * <cache>/logs/.worker-<kind>-XXXXXX files behind, and killed clients left
 * <tmp>/cbm-search-XXXXXX scratch directories. Sweeps run at daemon
 * bootstrap, on the first supervised worker spawn, and after each skip-log
 * write. Every threshold is a constant on purpose (fork spec: solve
 * "unbounded" first; parameterize only on real usage feedback). */

/* Per project kept, newest first — the rest removed. */
#define CBM_CACHE_SWEEP_SKIP_LOGS_KEEP 10
/* Older than this is removed even inside the keep window. */
#define CBM_CACHE_SWEEP_SKIP_LOGS_MAX_AGE_S (30LL * 24 * 60 * 60)
/* Age floor for worker/scratch temp files; a live run's files are young, so
 * the sweep never races an active index. */
#define CBM_CACHE_SWEEP_TMP_MAX_AGE_S 3600LL

/* Prune index-skip logs in <cache_dir>/logs. A skip log is a file named
 * <project>-<epoch>.log; the trailing dash-digits group is the epoch and the
 * project itself may contain dashes and digits. Per project keep the most
 * recent CBM_CACHE_SWEEP_SKIP_LOGS_KEEP and remove anything older than
 * CBM_CACHE_SWEEP_SKIP_LOGS_MAX_AGE_S. Returns the number of files removed,
 * or -1 when the logs directory cannot be read (fail-loud at the call site's
 * discretion; sweeping is best-effort housekeeping). */
int cbm_cache_sweep_skip_logs(const char *cache_dir);

/* Remove supervisor temp files (<cache_dir>/logs/.worker-*) older than
 * CBM_CACHE_SWEEP_TMP_MAX_AGE_S. Returns the number removed. */
int cbm_cache_sweep_worker_temp(const char *cache_dir);

/* Remove <tmp>/cbm-search-* scratch directories older than
 * CBM_CACHE_SWEEP_TMP_MAX_AGE_S (the two scratch files inside, then the
 * directory). Returns the number removed. */
int cbm_cache_sweep_search_scratch(void);

/* All of the above. Removal races with a concurrently finishing run are
 * harmless — worst case a just-dead file survives until the next sweep. */
int cbm_cache_sweep_run(const char *cache_dir);

#endif /* CBM_CACHE_SWEEP_H */
