# 什么是 HTTP 协议

## HTTP REST
> [!important]
> **HTTP REST API 就是“用 HTTP 协议、按 REST 架构风格来暴露和操作资源”的 API**。 `REST` 本身不是协议，也不是标准，而是一组架构约束；HTTP 只是实现它最常用的协议。

### REST 的核心约束
`REST` 由 `Roy Fielding` 在博士论文中提出，主要约束包括：
- **客户端-服务器分离**：前端和后端职责分离，各自独立演进。
- **无状态**：每个请求都必须包含服务器处理它所需的全部信息，服务器不依赖之前的请求保存会话状态。认证 token 通常每次请求都带上。
- **可缓存**：响应应明确是否可缓存，利用 `Cache-Control`、`ETag`、`Last-Modified` 等。
- **统一接口**：这是 REST 最关键的部分：
  - 每个**资源**用 `URI` 标识，例如 `/users/123`；
  - 通过**表示**操作资源，例如 `JSON`；
  - 消息自描述，例如用 `Content-Type`、`HTTP 方法`、`状态码`表达语义；
  - HATEOAS：响应中可包含相关操作链接，作为应用状态引擎。
- **分层系统**：客户端不需要知道是否经过网关、代理、负载均衡。
- **按需代码**：可选，服务器可下发可执行代码，如 `JavaScript`。

### 资源、方法和状态码
REST 把一切抽象为资源，URI 用名词标识资源，HTTP 方法表达操作意图：

| 方法   | 典型语义          | 安全性 | 幂等性 |
| ------ | ----------------- | ------ | ------ |
| GET    | 获取资源          | 安全   | 幂等   |
| POST   | 创建资源/触发处理 | 不安全 | 不保证 |
| PUT    | 整体替换资源      | 不安全 | 幂等   |
| PATCH  | 部分更新资源      | 不安全 | 不保证 |
| DELETE | 删除资源          | 不安全 | 幂等   |

“安全”指不改变服务器状态；“幂等”指执行多次与执行一次效果相同。
常见状态码：
- 200 OK
- 201 Created
- 204 No Content
- 400 Bad Request
- 401 Unauthorized
- 403 Forbidden
- 404 Not Found
- 409 Conflict
- 422 Unprocessable Entity
- 500 Internal Server Error


## 一个简单的例子
请求：
```http
GET /users/123
Accept: application/json
Authorization: Bearer <token>
```
响应：
```http
200 OK
Content-Type: application/json

{
  "id": 123,
  "name": "张三",
  "links": {
    "self": "/users/123",
    "orders": "/users/123/orders"
  }
}
```

所以，**HTTP REST API 是一种以资源为中心、利用 HTTP 方法和状态码、无状态、可缓存的接口设计风格**。实际工程中常说的“RESTful API”往往是“尽量遵循 REST 思想的 HTTP JSON API”，并不一定完整实现 HATEOAS 等所有约束。
