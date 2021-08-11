# 快速入门

[TOC]

## 下载安装

### 下载源码

``` bash
git clone <git repo url>
cd polaris-cpp
git submodule update --init
```

### 编译SDK

``` bash
# 编译
make
# 测试（可选）
make test

# 编译示例
make examples
```

在自己的项目中编译并引入Polaris CPP SDK的方法查看[编译&引入](Building.md)

## 服务发现示例

如果只需测试以及服务的服务发现，只是要以下命令测试：
``` bash
./build/examples/get_route <service_namespace> <service_name>
```

## ECHO服务示例

### 示例说明

ECHO服务，包括服务提供方和服务注册方两个程序。
服务提供方启动一个UDP ECHO服务端，并将服务注册到Polaris。
服务消费方启动一个UDP ECHO客户端，通过Polaris发现服务提供方，连接ECHO服务端，发送ECHO请求。

例子代码在`examples/`目录下：

- `echo_provider.cpp` echo服务提供方
- `echo_consumer.cpp` echo服务消费方

### 创建服务

在开始之前，需要到[Polaris OSS](http://polaris.oa.com)服务列表页面创建服务。用于测试的服务可创建在Test命名空间下。
创建成功需要记下弹出页面显示的服务token。token用于注册服务实例。


### 服务注册

服务提供方通过服务注册功能向服务中添加服务实例。服务注册需要提供服务的命名空间、服务名和服务Token。
并使用host和port来标示服务下的服务实例。可使用如下命令以不同方式启动服务提供方。

``` bash
# 注册服务实例，并开启心跳上报。其中前三个参数为自己创建的命名空间、服务名、服务token
# host和port分别为echo服务监听的地址和端口
./build/examples/echo_provider <service_namespace> <service_name> <service_token> <host> <port>

# 只注册服务实例，不开启心跳上报
./build/examples/echo_provider <service_namespace> <service_name> <service_token> <host> <port> no_heartbeat
```

### 服务发现

``` bash
# 发现服务实例并进行调用
./build/examples/echo_consumer  <service_namespace> <service_name>
```

## 使用手册

更详细的SDK使用说明可查看[使用手册](UserGuide.md)
