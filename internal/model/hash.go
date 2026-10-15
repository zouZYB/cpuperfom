package model

import (
	"crypto/sha256"
	"encoding/hex"
	"strings"
)

// sha256Of 拼接字段取 SHA-256，用于环境快照去重等场景。
func sha256Of(parts ...string) string {
	sum := sha256.Sum256([]byte(strings.Join(parts, "\x00")))
	return hex.EncodeToString(sum[:])
}

// HashToken 对 API token 做哈希存储，避免明文落库。
func HashToken(token string) string { return sha256Of(token) }
