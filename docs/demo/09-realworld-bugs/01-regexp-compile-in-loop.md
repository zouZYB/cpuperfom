# 01 真实世界性能 Bug:循环里重复编译正则

> 对应笔记:第九章-15《A Large Scale Study of Hundreds of In-Depth Performance Bug Reports》
> (Jin et al., PLDI 2012:对真实开源项目性能 bug 的系统性研究)

## 目的

Jin et al. 从 Apache、Mozilla 等 109 个真实性能 bug 报告中归纳出模式,**最常见的一类
是"重复计算"**:循环体内每次都做一件结果不变的昂贵工作。最经典的具体形式就是
"每次调用都编译同一个正则"。本例复现它,实测 **58x**。

## 原理

```
regexp.MustCompile(pattern) 的开销:
  解析 → 语法树 → 编译成有限自动机程序 → 几千纳秒~微秒级

regexp.MatchString(pattern, s) 等价于:
  MustCompile(pattern).MatchString(s)   // 每次调用都重新编译!

正确姿势:包级变量 + init/MustCompile 一次,复用自动机
  var re = regexp.MustCompile(pattern)   // 编译一次
  re.MatchString(s)                      // 只付匹配的钱
```

## 代码(main.go)

```go
package main

import (
	"fmt"
	"regexp"
	"time"
)

const pattern = `^user_\d+@example\.com$`

var emails = []string{
	"user_123@example.com", "not-an-email", "user_42@example.com",
	"user_999@example.com", "admin", "user_7@example.com",
}

// 反例:每次调用都重新编译同一个正则
func validateBad(emails []string) int {
	ok := 0
	for _, e := range emails {
		if regexp.MustCompile(pattern).MatchString(e) {
			ok++
		}
	}
	return ok
}

// 正例:包级变量,init 时编译一次
var userRe = regexp.MustCompile(pattern)

func validateGood(emails []string) int {
	ok := 0
	for _, e := range emails {
		if userRe.MatchString(e) {
			ok++
		}
	}
	return ok
}

func bench(name string, f func() int) {
	const reps = 20000
	f() // 预热
	start := time.Now()
	sink := 0
	for i := 0; i < reps; i++ {
		sink += f()
	}
	total := time.Since(start)
	fmt.Printf("%-10s %v/次  (校验结果 %d)\n", name, total/reps, sink/reps)
}

func main() {
	bench("循环内编译", func() int { return validateBad(emails) })
	bench("预编译复用", func() int { return validateGood(emails) })
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
循环内编译 20.26µs/次  (校验结果 4)
预编译复用 350ns/次    (校验结果 4)   → 58x
```

校验结果一致,速度差 50 倍以上——且差距随 pattern 复杂度增大(长 pattern 编译更贵)。
(数字为当前机器负载下实测;安静环境下曾测得 1.857µs vs 50ns = 37x,结论相同。)

## 分析要点

- 这类 bug 的特征:**正确性测试全过,代码审查不易察觉**(一行看起来无害的调用),
  只有 profiling 才能抓到——CPU 火焰图里 `regexp/syntax` 相关的帧占了大头就是它
- Jin et al. 的论文要点:性能 bug 与正确性 bug 不同,**不会因测试失败而暴露**;
  三大来源:重复计算、循环内不必要分配(见 [02](02-string-concat-allocation.md))、
  无谓的同步/IO。本例与 02 合起来覆盖了前两大来源
- Go 生态同形 bug:`time.Parse` 热路径、`fmt.Sprintf` 拼固定格式、`sql.Stmt` 不 prepare
- 防御手段:热路径代码评审时问"这个值每轮都变吗?";持续 profiling(见
  [04-01](../04-profiling/01-sampling-profiler-pprof.md))让这类回归在 CI 里现形

## 常见误区

- ❌ "反正 pattern 简单,编译很快" —— 编译开销是解析+自动机构建,与输入串长短无关;
  且在微秒级服务里 1.8µs 就是整个请求预算
- ❌ 只在 benchmark 里验证逻辑正确性 —— 性能 bug 的正确性完全正常,必须配 profiling
- ❌ 把 MustCompile 藏进"工具函数"里 —— 函数内每次调用还是会重新编译,坑更隐蔽
