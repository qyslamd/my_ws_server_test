# EngSketchPad 源码（C/C++）

## 来源

| 项 | 内容 |
|---|---|
| 上游 | EngSketchPad（The Engineering Sketch Pad）发行包 |
| 本地来源 | `3rdparty/EngSketchPad-master/src`（该目录按 .gitignore 不入库） |
| 文件日期 | 2012-10-20 |
| 规模 | 159 个文件，约 3.5 MB |

## 目录说明

```text
esp-src/
├── EGADS/          Electronic Geometry Aircraft Design System
│   ├── include/    公共头文件（egads.h、egadsTypes.h、wsserver.h …）
│   ├── src/        C 实现
│   └── examples/   示例程序
├── OpenCSM/        Open Computational Structural Modeler（C）
├── wvServer/       WebViewer WebSocket 服务端（C，源自 libwebsockets）
├── Makefile        Unix 构建
├── NMakefile       Windows 构建
└── make.bat
```

## 相对上游的改动

| 处理 | 内容 | 说明 |
|---|---|---|
| 删除 | `*.o` 编译产物（42 个，约 5.4 MB） | 仅保留源码，避免入库二进制 |

除此之外未修改任何源码。

## 许可（按子目录）

| 子目录 | 许可 | 版权 |
|---|---|---|
| EGADS | **LGPL 2.1** | Copyright 2011-2012, Massachusetts Institute of Technology |
| OpenCSM | **LGPL 2.1** | Copyright 2011, John F. Dannenhoffer, III (Syracuse University) |
| wvServer | **LGPL 2.1**（libwebsockets 及其附加文件） | Copyright 2010 Andy Green；base64-decode 另附 Bob Trower 条款 |

> LGPL 2.1 的影响：可链接进闭源程序，但需满足 LGPL 条款（动态链接，或静态链接时
> 提供可重新链接的目标文件，并保留版权与许可声明）。对外交付前请由法务确认链接方式。

## 构建说明

- 依赖 **OpenCASCADE 6.5.x**（发行包 README 指定 6.5.2 兼容性最好）。
- Unix 用 `make`，Windows 用 `nmake /f NMakefile`；**没有 CMake 工程**。
- 接入本仓库的 CMake 构建需另写包装，当前未接入。

## 备注

- 目录名用 `esp-src` 而非 `EngSketchPad-src`：仓库 `.gitignore` 有
  `/3rdparty/EngSketchPad-*/` 规则（忽略上游解压目录），改名可避免被忽略。
