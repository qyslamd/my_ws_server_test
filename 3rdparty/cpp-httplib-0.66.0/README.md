# cpp-httplib 0.66.0（只给测试用）

上游：<https://github.com/yhirose/cpp-httplib>
许可：MIT，见同目录 `LICENSE`
文件：只有 `httplib.h` 一个头文件

## 为什么在这里

产品服务端用的是 mongoose。测试需要一个**独立实现**的客户端去连它，
否则"两边都用同一套代码"测不出协议问题。cpp-httplib 自带 HTTP 与
WebSocket 客户端，正好干这件事。

它**不参与产品构建**：不要把它链进 `v1_base`、apps 或 examples，
只有在 `src/tests/integration/` 里的测试目标可以引用。

## 用之前要知道

- 它是异常风格（`Result`、`std::exception`），与仓库"失败走
  `bool + std::string& err`"的约定相反。所以只让测试目标开 `/EHsc`，
  别把它 include 进任何产品头文件。
- 单头 862 KB，编译一次要几秒。一个测试可执行文件只 include 一次；
  要是有多个测试文件需要它，先抽一层薄封装，不要每个文件都 include。
- 升级时整个目录替换，版本号改在目录名上（仓库惯例）。
