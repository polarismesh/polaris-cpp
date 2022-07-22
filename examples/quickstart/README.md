# 快速开始样例

## 样例说明

样例演示如何使用 polaris-cpp 完成被调端以及主调端应用接入polaris，并完成服务调用流程。

consumer: 接收用户tcp请求，通过polaris发现provider服务，并将请求转发给provider处理
provider：启动服务监听端口，并自身注册到北极星，且开启健康检查

## 编译

```bash
make clean
make
```

编译完成后，会在当前目录生成两个二进制文件，consumer和provider

## 运行

执行样例

需要在控制台先创建被调服务 Test/quickstart.echo.service

启动被调方：

```bash
# 监听地址端口 127.0.0.1:9091  注册到服务 Test/quickstart.echo.service 服务token为xxx
./provider Test quickstart.echo.service xxx 127.0.0.1 9092
```

启动主调方：

```bash
# 监听地址端口 127.0.0.1:9093  转发请求到被调服务 Test/quickstart.echo.service
./consumer 127.0.0.1 9093 Test quickstart.echo.service
```

发送测试请求：

```bash
# 向consumer监听端口发送hello
echo hello | nc 127.0.0.1 9093
```
