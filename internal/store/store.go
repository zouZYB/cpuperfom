// Package store 提供 SQLite 数据访问层（WAL 模式，单写多读）。
// SQL 保持标准语法，便于后期迁移 PostgreSQL。
package store

import (
	"database/sql"
	"embed"
	"fmt"

	"github.com/pressly/goose/v3"
	_ "modernc.org/sqlite"
)

//go:embed all:migrations
var migrationsFS embed.FS

// Open 打开（必要时创建）数据库并执行迁移。
// path 为 SQLite 文件路径，":memory:" 用于测试。
func Open(path string) (*sql.DB, error) {
	// WAL + busy_timeout：多读单写并发安全
	dsn := fmt.Sprintf("file:%s?_pragma=journal_mode(WAL)&_pragma=busy_timeout(5000)&_pragma=foreign_keys(1)", path)
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, fmt.Errorf("open sqlite: %w", err)
	}
	// SQLite 写入串行化：单写连接避免 SQLITE_BUSY
	db.SetMaxOpenConns(1)

	goose.SetBaseFS(migrationsFS)
	if err := goose.SetDialect("sqlite3"); err != nil {
		return nil, fmt.Errorf("goose dialect: %w", err)
	}
	if err := goose.Up(db, "migrations"); err != nil {
		return nil, fmt.Errorf("migrate: %w", err)
	}
	return db, nil
}
