// benchd 是 cpuperfom 平台服务器：REST API + 前端静态资源托管。
// 传入 -ci ci.yaml 时同时启动内置 CI 监控（git 轮询 → 自动性能对比）。
package main

import (
	"context"
	"flag"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/cpuperfom/cpuperfom/internal/api"
	"github.com/cpuperfom/cpuperfom/internal/ci"
	"github.com/cpuperfom/cpuperfom/internal/store"
	"github.com/cpuperfom/cpuperfom/web"
)

func main() {
	addr := flag.String("addr", ":8080", "HTTP listen address")
	dbPath := flag.String("db", "cpuperfom.db", "SQLite database path")
	ciPath := flag.String("ci", "", "持续性能监控配置（ci.yaml），为空则不启用")
	flag.Parse()

	db, err := store.Open(*dbPath)
	if err != nil {
		log.Fatalf("open db: %v", err)
	}
	defer db.Close()
	q := store.New(db)

	srv := api.NewServer(q)

	// 托管嵌入的 Web 看板（web/dist 由 npm run build 生成）
	if distFS, err := fs.Sub(web.Dist, "dist"); err == nil {
		srv.SetStatic(http.FS(distFS))
	} else {
		log.Printf("web dashboard unavailable: %v", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// 可选：内置 CI 监控（不传 -ci 时行为与纯 API 服务一致）
	if *ciPath != "" {
		ciCfg, err := ci.LoadConfig(*ciPath)
		if err != nil {
			log.Fatalf("load ci config: %v", err)
		}
		svc, err := ci.NewService(ciCfg, q)
		if err != nil {
			log.Fatalf("init ci service: %v", err)
		}
		go svc.Start(ctx)
		log.Printf("ci watcher: %d 个仓库已纳入监控（%s）", len(ciCfg.Repos), *ciPath)
	}

	httpSrv := &http.Server{
		Addr:              *addr,
		Handler:           srv.Handler(),
		ReadHeaderTimeout: 10 * time.Second,
	}
	go func() {
		log.Printf("benchd listening on %s (db=%s)", *addr, *dbPath)
		if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http server: %v", err)
		}
	}()

	<-ctx.Done()
	log.Printf("benchd shutting down…")
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := httpSrv.Shutdown(shutdownCtx); err != nil {
		log.Printf("http shutdown: %v", err)
	}
}
