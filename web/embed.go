// Package web 把前端构建产物嵌入 benchd 单二进制。
package web

import "embed"

//go:embed all:dist
var Dist embed.FS
