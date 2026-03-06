# UDP-wrapped transport prototype

这是一个从零开始的最小原型，用来验证你描述的架构：

- 本机一个通信代理进程 `agent`
- `agent` 接收 UDP 包
- 按来源 `IP:port` + `connection_id` 分流到独立缓冲区
- 用户进程通过 IPC(Unix Domain Socket)订阅某个 peer key
- `agent` 将该 peer 的缓冲数据转发给对应用户进程

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run (Three-process prototype)

1) 启动代理（仅本地 IPC 验证，不创建 UDP socket）

```bash
./build/agent --ipc-only /tmp/transport_agent.sock
```

2) 启动接收进程（receiver），订阅一个 key（`ip port connection_id`）

```bash
./build/receiver /tmp/transport_agent.sock 127.0.0.1 54321 7
```

3) 启动发送进程（sender），通过 agent 发送

```bash
./build/sender /tmp/transport_agent.sock 127.0.0.1 54321 7 hello-via-agent
```

你会看到三端输出：
- `sender` 输出发送动作和 agent 回包
- `agent` 输出收到 SEND、匹配 key、分发情况
- `receiver` 输出收到的数据长度和内容

`receiver` 侧示例输出：

```text
[receiver] recv bytes=...
FROM 127.0.0.1:54321#7 15
hello-via-agent
```

说明：
- 目前 `sender` 的 payload 参数是单个命令行参数（如需空格请自行加引号）
- 若 agent 不是 `--ipc-only` 模式，会尝试把 SEND 同步发成 UDP 包（并在日志标注 `udp_sent=yes/no`）

## Current limitations

- 目前只做了“分流+缓冲+IPC 转发”，还没有可靠传输机制（重传/滑窗/拥塞控制）
- peer 维度为 `src ip:port + connection_id`
- IPC 协议是最小文本协议，后续可改成二进制 framing
- 缓冲采用内存队列，达到上限会丢弃旧包

## Next suggested protocol milestones

1. 定义完整包头（连接 ID、流 ID、时间戳、校验）
2. 增加 ACK/NACK 与超时重传
3. 增加发送窗口与乱序重排
4. 增加拥塞控制与流量整形
5. 将用户 API 封装成 `libtransport.so`
