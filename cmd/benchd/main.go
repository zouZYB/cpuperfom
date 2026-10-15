// benchd 是 cpuperfom 平台服务器：REST API + 前端静态资源托管。
package main

import (
	"flag"
	"io/fs"
	"log"
	"net/http"
	"time"

	"github.com/cpuperfom/cpuperfom/internal/api"
	"github.com/cpuperfom/cpuperfom/internal/store"
	"github.com/cpuperfom/cpuperfom/web"
)

func main() {
	addr := flag.String("addr", ":8080", "HTTP listen address")
	dbPath := flag.String("db", "cpuperfom.db", "SQLite database path")
	flag.Parse()

	db, err := store.Open(*dbPath)
	if err != nil {
		log.Fatalf("open db: %v", err)
	}
	defer db.Close()

	srv := api.NewServer(store.New(db))

	// 托管嵌入的 Web 看板（web/dist 由 npm run build 生成）
	if distFS, err := fs.Sub(web.Dist, "dist"); err == nil {
		srv.SetStatic(http.FS(distFS))
	} else {
		log.Printf("web dashboard unavailable: %v", err)
	}

	httpSrv := &http.Server{
		Addr:              *addr,
		Handler:           srv.Handler(),
		ReadHeaderTimeout: 10 * time.Second,
	}
	log.Printf("benchd listening on %s (db=%s)", *addr, *dbPath)
	log.Fatal(httpSrv.ListenAndServe())
}
