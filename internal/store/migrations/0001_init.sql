-- +goose Up
-- cpuperfom 初始 schema（M1：核心表全部建立，后续里程碑只做扩展）

CREATE TABLE users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT NOT NULL UNIQUE,
    email         TEXT NOT NULL DEFAULT '',
    password_hash TEXT NOT NULL DEFAULT '',
    role          TEXT NOT NULL DEFAULT 'member' CHECK (role IN ('admin','member','viewer')),
    created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

CREATE TABLE api_tokens (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL REFERENCES users(id),
    name       TEXT NOT NULL,
    token_hash TEXT NOT NULL UNIQUE,
    scopes     TEXT NOT NULL DEFAULT 'upload',
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    last_used_at TEXT
);

CREATE TABLE projects (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT NOT NULL UNIQUE,
    repo_url   TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now'))
);

-- 环境快照：同一环境只存一条，按 hash 去重
CREATE TABLE environments (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    hash          TEXT NOT NULL UNIQUE,
    cpu_model     TEXT NOT NULL DEFAULT '',
    microarch     TEXT NOT NULL DEFAULT '',
    cores         INTEGER NOT NULL DEFAULT 0,
    freq_governor TEXT NOT NULL DEFAULT '',
    os            TEXT NOT NULL DEFAULT '',
    kernel        TEXT NOT NULL DEFAULT '',
    compiler      TEXT NOT NULL DEFAULT '',
    flags         TEXT NOT NULL DEFAULT ''
);

CREATE TABLE runs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id  INTEGER NOT NULL REFERENCES projects(id),
    commit_sha  TEXT NOT NULL DEFAULT '',
    ref         TEXT NOT NULL DEFAULT '',
    label       TEXT NOT NULL DEFAULT 'ci' CHECK (label IN ('before','after','baseline','ci')),
    ci_provider TEXT NOT NULL DEFAULT '',
    ci_run_url  TEXT NOT NULL DEFAULT '',
    env_id      INTEGER REFERENCES environments(id),
    started_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    finished_at TEXT
);
CREATE INDEX idx_runs_project ON runs(project_id, started_at);

CREATE TABLE benchmarks (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL REFERENCES projects(id),
    name       TEXT NOT NULL,
    kind       TEXT NOT NULL DEFAULT 'prog' CHECK (kind IN ('func','prog')),
    unit       TEXT NOT NULL DEFAULT 'ns',
    UNIQUE (project_id, name)
);

-- 原始迭代样本：统计显著性检验的数据基础
CREATE TABLE samples (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id       INTEGER NOT NULL REFERENCES runs(id),
    benchmark_id INTEGER NOT NULL REFERENCES benchmarks(id),
    iteration    INTEGER NOT NULL,
    value_ns     REAL NOT NULL
);
CREATE INDEX idx_samples_run ON samples(run_id, benchmark_id);

-- 硬件计数器（Linux perf stat；Windows 可为空）
CREATE TABLE counters (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id       INTEGER NOT NULL REFERENCES runs(id),
    benchmark_id INTEGER NOT NULL REFERENCES benchmarks(id),
    name         TEXT NOT NULL,
    value        REAL NOT NULL,
    UNIQUE (run_id, benchmark_id, name)
);

-- 火焰图 folded stack 数据（差分火焰图源数据）
CREATE TABLE profiles (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id       INTEGER NOT NULL REFERENCES runs(id),
    benchmark_id INTEGER NOT NULL REFERENCES benchmarks(id),
    format       TEXT NOT NULL DEFAULT 'folded',
    data         BLOB NOT NULL,
    UNIQUE (run_id, benchmark_id, format)
);

CREATE TABLE baselines (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id   INTEGER NOT NULL REFERENCES projects(id),
    benchmark_id INTEGER NOT NULL REFERENCES benchmarks(id),
    run_id       INTEGER NOT NULL REFERENCES runs(id),
    set_by       TEXT NOT NULL DEFAULT '',
    created_at   TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    UNIQUE (project_id, benchmark_id)
);

CREATE TABLE alert_rules (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id    INTEGER NOT NULL REFERENCES projects(id),
    benchmark_id  INTEGER REFERENCES benchmarks(id), -- NULL = 项目级默认规则
    metric        TEXT NOT NULL DEFAULT 'median_ns',
    threshold_pct REAL NOT NULL DEFAULT 5.0,
    window        INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE alerts (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id     INTEGER NOT NULL REFERENCES alert_rules(id),
    run_id      INTEGER NOT NULL REFERENCES runs(id),
    status      TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','acknowledged','resolved')),
    detail      TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
    notified_at TEXT
);

-- +goose Down
DROP TABLE IF EXISTS alerts;
DROP TABLE IF EXISTS alert_rules;
DROP TABLE IF EXISTS baselines;
DROP TABLE IF EXISTS profiles;
DROP TABLE IF EXISTS counters;
DROP TABLE IF EXISTS samples;
DROP TABLE IF EXISTS benchmarks;
DROP TABLE IF EXISTS runs;
DROP TABLE IF EXISTS environments;
DROP TABLE IF EXISTS projects;
DROP TABLE IF EXISTS api_tokens;
DROP TABLE IF EXISTS users;
