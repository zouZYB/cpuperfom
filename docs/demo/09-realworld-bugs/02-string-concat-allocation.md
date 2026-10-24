# 02 真实世界性能 Bug:循环内字符串拼接的 O(n²) 分配

> 对应笔记:第九章-15(Jin et al., PLDI 2012:性能 bug 第二大来源——
> 循环内不必要分配/复制)

## 目的

`s += part` 看起来人畜无害,但字符串**不可变**:每次拼接都要分配新数组并拷贝
全部旧内容。拼 n 次的总拷贝量是 1+2+...+n = **O(n²)**。本例用 Go 的
`strings.Builder`(+ `Grow` 预分配)对比,实测 **~4000x**——这是真实项目里
最容易被 code review 放过的"渐近复杂度"bug。

## 原理

```
s += part 第 k 轮:  alloc(len(s)+len(part)) + copy(旧 s) + copy(part)
n 轮总拷贝 ≈ n²·L/2 (L=每段长度)   → n=20000 时 ~2 亿字节

strings.Builder + Grow(预估总长):
  一次性分配足够大的 buffer,之后只做 append(尾部写)  → O(n) 总拷贝

同样的坑在 C++:std::string operator+= 在容量不足时 realloc(均摊 O(1),
其实还好);真正的 C++ 版本是循环里 std::string + std::string 生成临时对象。
Java:String += 在循环里是教科书级反例(编译器建 StringBuilder 但每轮新建)。
```

## 代码(main.go)

```go
package main

import (
	"fmt"
	"strings"
	"time"
)

const reps = 20000

// 反例:每次 += 都分配新字符串并拷贝全部旧内容 → 总拷贝 O(n²)
func buildBad() string {
	s := ""
	for i := 0; i < reps; i++ {
		s += "0123456789"
	}
	return s
}

// 正例:Builder 尾部追加 + Grow 预分配 → 总拷贝 O(n)
func buildGood() string {
	var b strings.Builder
	b.Grow(reps * 10) // 预估总长度,一次分配
	for i := 0; i < reps; i++ {
		b.WriteString("0123456789")
	}
	return b.String()
}

func bench(name string, f func() string) {
	const window = 500
	var sink int
	f() // 预热
	start := time.Now()
	for i := 0; i < window; i++ {
		sink = len(f())
	}
	total := time.Since(start)
	fmt.Printf("%-16s %v/次  (结果长度 %d)\n", name, total/window, sink)
}

func main() {
	bench("+= 拼接(O(n²))", buildBad)
	bench("Builder+Grow(O(n))", buildGood)
}
```

## 运行

```bash
go run main.go
```

## 预期结果(本仓库开发机实测)

```
+= 拼接(O(n²))      121.01ms/次  (结果长度 200000)
Builder+Grow(O(n))  30.65µs/次   (结果长度 200000)   → 3948x
```

结果长度一致,速度差约 4000 倍,且**随 reps 增大差距继续拉大**(一个是平方一个是线性)。

## 分析要点

- 这是**渐近复杂度**问题,不是常数问题:n 小时两者都快,压测通过,上线数据量
  一涨就炸——正是 Jin et al. 说的"测试发现不了"型性能 bug
- 4000x 的构成:2 亿字节的分配+拷贝 vs 20 万字节的纯写——GC 压力也是天壤之别
  (前者制造 2 亿字节垃圾)
- 识别信号:pprof 火焰图里 `runtime.memmove` / `runtime.mallocgc` 占大头,
  而你的"业务逻辑"帧很薄——在搬运,没在计算
- 同类同构 bug:slice 不用 `make([]T, 0, cap)` 预分配(append 翻倍扩容还好,
  但热点里仍可省)、map 不给 size hint、bytes.Buffer 不 Grow
- Go 1.20+ 编译器其实能把部分简单 `+=` 循环转成 Builder,但条件苛刻
  (单变量、无中间使用),**不要依赖**:显式 Builder 才是契约

## 常见误区

- ❌ "现代分配器很快,分配不怕" —— 怕的不是分配本身,是 O(n²) 的**拷贝**
- ❌ 以为 strings.Builder 不用 Grow 也一样 —— 不 Grow 是均摊 O(n) 但常数大
  (多次翻倍+拷贝),Grow 后才是真正的一次分配
- ❌ 用 `fmt.Sprintf` 在热循环里拼 —— 它内部走反射,比 += 还贵;格式化是给人看的,
  拼接是给机器跑的
