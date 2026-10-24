-- +goose Up
-- benchd 内置 CI 监控：受监控仓库的轮询游标（每个 (path, branch) 一行）
CREATE TABLE watch_repos (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    name         TEXT NOT NULL,
    path         TEXT NOT NULL,
    branch       TEXT NOT NULL DEFAULT 'main',
    last_commit  TEXT NOT NULL DEFAULT '',   -- 上次已处理的 commit SHA，'' = 未初始化
    last_seen    TEXT NOT NULL DEFAULT '',   -- 上次轮询看到的 HEAD SHA
    last_poll_at TEXT,
    last_error   TEXT NOT NULL DEFAULT '',
    created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    UNIQUE (path, branch)
);

-- 「新提交 → 对比任务」历史
CREATE TABLE ci_jobs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    watch_repo_id INTEGER NOT NULL REFERENCES watch_repos(id),
    before_sha    TEXT NOT NULL,
    after_sha     TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'running' CHECK (status IN ('running','success','failed')),
    before_run_id INTEGER REFERENCES runs(id),
    after_run_id  INTEGER REFERENCES runs(id),
    verdict       TEXT NOT NULL DEFAULT '',  -- 对比结论摘要，如 "matmul: 变慢 8.2%（显著）"
    error         TEXT NOT NULL DEFAULT '',
    started_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    finished_at   TEXT
);
CREATE INDEX idx_ci_jobs_repo ON ci_jobs(watch_repo_id, started_at);

-- +goose Down
DROP TABLE IF EXISTS ci_jobs;
DROP TABLE IF EXISTS watch_repos;
