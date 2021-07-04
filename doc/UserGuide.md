# 用户手册

本文档提供Polaris CPP SDK的相关使用说明

[TOC]

## 快速入门

参考[快速入门](QuickStart.md)文档

## 编译&引入

在自己项目中编译并引入Polaris CPP SDK的方法参见[编译&引入](Building.md)

## Polaris文档

使用SDK之前，需要先创建通过[Polaris OSS](polaris.oa.com)创建服务。Polaris概念和Polaris OSS的使用参考Polaris文档

## SDK基本概念

- Context：SDK上下文，通过配置创建，管理插件和数据。Provider Api和Consumer Api都需要基于Context创建
- Provider Api：用于服务注册、反注册、心跳上报
- Consumer Api：用于服务发现
- Plugin Api：插件接口，用于实现自定义功能。实现自定义负载均衡、探活及熔断等功能。

## 接口说明

一般情况下用户只需要使用以下两个头文件

- 服务被调方/提供者：polaris/provider.h
- 服务主调方/消费者：polaris/consumer.h

其他头文件包含实现特殊需求的接口：

- polaris/log.h 日志接口。支持设置SDK日志级别和路径。可自定义实现日志类替换SDK默认日志类

所有头文件均包含注释，用户可直接查看文件查看接口说明

## 服务被调方/提供者接口

服务被调方通过在北极星上创建服务，并将服务实例注册到北极星上提供给其他服务消费。
使用的接口包含在polaris/provider.h头文件中。

### Provider对象创建

业务程序在调用相关接口前必须先创建ProviderApi对象。ProviderApi实际是封装了SDK的Context对象，
Context对象则是通过配置对象Config创建。而Config对象的创建则可以通过文件或者字符串创建。
注1：**ProviderApi对象是线程安全的，一个进程只需要创建一个即可。**
注2：**如果程序即使用ProviderApi又使用ConsumerApi，则通过共享Context方式创建ProviderApi**

SDK提供了多个接口封装上述过程以便创建ProviderApi以适用不同场景。

1. 通过默认配置文件创建ProviderApi对象。如果是程序只是被调方，则推荐这种方式创建
    ```c++
    // 这个方法默认加载当前目录下的`polaris.yaml`配置文件初始化Context来创建ProviderApi。
    // 如果该配置文件不存在，则使用默认配置；否则，加载该文件的配置项覆盖相关默认配置。
    polaris::ProviderApi* provider_api = polaris::ProviderApi::CreateWithDefaultFile();
    if (provider_api == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }
    // 使用provider_api

    // 不再使用后，释放provider_api
    delete provider_api;
    ```

2. 如果程序除了是被调端还需要作为主调端访问其他服务，则使用通过Context创建ProviderApi对象。
    ```c++
    // 先创建Config
    polaris::Config* config = polaris::Config::CreateWithDefaultFile();
    if (config == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }
    // 先创建Context
    polaris::Context* context = polaris::Context::Create(config);
    delete config;  // 创建完成后即可释放config对象
    if (context == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }
    // 再以共享模式Context创建ProviderApi，用户自己维护Context的生命周期，该context还可以用于创建ConsumerApi
    polaris::ProviderApi* provider_api = polaris::ProviderApi::Create(context);
    if (provider_api == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }

    // 使用完成释放，provider api必须在context之前释放
    delete provider_api;
    delete context;
    ```

3. 其他创建方式则用于支持特殊场景创建ProviderApi
    - 通过配置对象创建，而配置对象有可以通过文件和字符串创建
    - 通过指定文件创建
    - 通过指定字符串创建
   上述创建方法具体说明参见头文件polaris/provider.h

### 服务注册

服务注册接口用于向服务中注册服务实例。服务注册必须带上服务token，可以到控制台查看服务的token。
此外可以配置是否开启健康检查，对于开启了健康检查的服务实例，注册完成后必须定期心跳上报接口维持自身的健康状态
```c++
std::string service_namespace = "Test";
std::string service_name = "polaris.cpp.sdk.test";
std::string service_token = "****************";
std::string host = "127.0.0.1";
int port = 9092;
polaris::InstanceRegisterRequest register_req(service_namespace, service_name, service_token, host, port);

// 设置开启健康检查，不设置默认为不开启
register_req.SetHealthCheckFlag(true);
register_req.SetHealthCheckType(polaris::kHeartbeatHealthCheck);
register_req.SetTtl(5);  // 心跳服务器超过1.5ttl时间未收到客户端上报心跳就将实例设置为不健康

// 如果服务实例在vpc环境，可选设置vpc id
register_req.SetVpcId("vpc_0");
// 其他参数参见polaris/provider.h

// 调用服务注册接口
std::string instance_id;  // 调用成功会返回instance_id
polaris::ReturnCode ret = provider->Register(register_req, instance_id);
if (ret != polaris::kReturnOk) {
    abort();
}

```

### 心跳上报
如果在服务注册的时候开启了上报心跳，则业务需要定时调用心跳上报接口维持服务健康状态
```c++
// instance_id为通过服务注册接口返回的服务实例ID
polaris::InstanceHeartbeatRequest heartbeat_req(service_token, instance_id);
while (true) {
    if (provider->Heartbeat(heartbeat_req) == kReturnOk) {
        sleep(5);
    }
}

```

### 服务反注册
服务退出时，可调用服务反注册接口将服务实例从服务的实例列表中删除
```c++
polaris::InstanceDeregisterRequest deregister_req(service_token, instance_id);
ret = provider->Deregister(deregister_req);
```


## 服务主调方/消费者接口

业务程序在调用相关接口前必须先创建ConsumerApi对象。ConsumerApi实际是封装了SDK的Context对象，
Context对象则是通过配置对象Config创建。而Config对象的创建则可以通过文件或者字符串创建。
注1：**ConsumerApi对象是线程安全的，一个进程只需要创建一个即可。**
注2：**如果程序即使用ProviderApi又使用ConsumerApi，则通过共享Context方式创建ConsumerApi**

SDK提供了多个接口封装上述过程以便创建ConsumerApi以适用不同场景。

1. 通过默认配置文件创建ConsumerApi对象。如果是程序只是主调方，则推荐这种方式创建
    ```c++
    // 这个方法默认加载当前目录下的`polaris.yaml`配置文件初始化Context来创建ConsumerApi。
    // 如果该配置文件不存在，则使用默认配置；否则，加载该文件的配置项覆盖相关默认配置。
    polaris::ConsumerApi* consumer_api = polaris::ConsumerApi::CreateWithDefaultFile();
    if (consumer_api == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }
    // 使用consumer_api

    // 不再使用后，释放consumer_api
    delete consumer_api;
    ```

2. 如果程序除了是主调端还需要作为被调端注册自己，则使用通过Context创建ConsumerApi对象。
    ```c++
    // 先创建Config
    polaris::Config* config = polaris::Config::CreateWithDefaultFile();
    if (config == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }
    // 先创建Context
    polaris::Context* context = polaris::Context::Create(config);
    delete config;  // 创建完成后即可释放config对象
    if (context == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }
    // 再以共享模式Context创建ConsumerApi，用户自己维护Context的生命周期，该context还可以用于创建ProviderApi
    polaris::ConsumerApi* consumer_api = polaris::ConsumerApi::Create(context);
    if (consumer_api == NULL) { // 创建错误，创建失败原因可查看日志~/polaris/log/polaris.log
        abort();
    }

    // 程序退出前完成释放，consumer api必须在context之前释放
    delete consumer_api;
    delete context;
    ```

3. 其他创建方式则用于支持特殊场景创建ConsumerApi
    - 通过配置对象创建，而配置对象有可以通过文件和字符串创建
    - 通过指定文件创建
    - 通过指定字符串创建
   上述创建方法具体说明参见头文件polaris/consumer.h

### 服务发现

1. 发现单个服务实例
   作为主调方一般场景下每次获取一个服务实例进行业务调用，在调用完成后上报调用结果
   获取单个服务实例：默认有规则先走规则路由，然后走就近路由选出一批实例，再进行负载均衡得到单个实例
   上报调用结果：上报调用结果主要用于熔断故障节点，并统计调用延迟等
   ```c++
    polaris::ServiceKey service_key = {"Test", "polaris.cpp.sdk.test"};
    polaris::GetOneInstanceRequest request(service_key);
    polaris::Instance instance;
    polaris::ReturnCode ret = consumer->GetOneInstance(request, instance);
    if (ret != polaris::kReturnOk) {
        std::cout << "get one instance with error: " << polaris::ReturnCodeToMsg(ret) << std::endl;
        abort();
    }
    // 获取实例的IP和Port
    std::string ip = instance.GetHost();
    int port = instance.GetPort();
    // 进行业务调用
    int ret_code = 0; // 记录调用返回码用于上报

    // 上报调用结果
    polaris::ServiceCallResult result;
    result.SetServiceNamespace(service_namespace);
    result.SetServiceName(service_name);
    result.SetInstanceId(instance.GetId());
    result.SetDelay(end - begin);
    result.SetRetCode(ret_code);
    // 只有网络或请求错误才上报错误，业务逻辑失败无需上报失败，否则无法正常进行故障剔除
    result.SetRetStatus(ret_code > 0 ? polaris::kCallRetOk : polaris::kCallRetError);
    if ((ret = consumer->UpdateServiceCallResult(result)) != polaris::kReturnOk) {
        std::cout << "update call result error: " << polaris::ReturnCodeToMsg(ret) << std::endl;
    }
    ```

## 功能接口

### 日志接口

日志接口提供用户操作SDK日志相关功能。注意：**日志相关接口必须在其他接口之前进行调用**
SDK默认提供了输出日志到文件的功能。用户可选择自定义日志实现或自定义日志级别和日志输出目录

#### 自定义日志级别或路径

``` c++
// 日志接口头文件
#include "polaris/log.h"

// 设置日志级别为TRACE级别，SDK自带日志默认日志级别为INFO，日志级别参见头文件
polaris::GetLogger()->SetLogLevel(polaris::kTraceLogLevel);

// 设置日志路径
polaris::SetLogDir("/tmp");
```

### 负载均衡接口

### 探测插件接口
