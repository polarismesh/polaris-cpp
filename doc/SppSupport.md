# SPP微线程使用说明

[TOC]

## 服务发现

Polaris SDK进行服务发现时会查询内存缓存是否有服务相关数据，如果内存缓存有服务的数据，则直接使用。API不会阻塞
如果没有服务数据，则向队列中发送拉取服务数据的请求，并阻塞等待数据就绪。
网络线程从队列获取服务数据拉取请求向Server查询，并在获取到数据写入内存缓存中时通知数据已就绪

Polaris SDK默认使用的数据通知对象由条件变量实现，等待数据就绪时不会进行协程切换。所以SPP需要自定义实现该接口类。

### 相关接口

数据就绪通知对象接口如下：
```c++
class DataNotify {
public:
    virtual ~DataNotify() {}

    // 通知服务数据加载完成，由网络线程使用，不能使用协程相关方法
    virtual void Notify() = 0;

    // 等待服务数据加载完成，多个协程可以使用同一个对象进行等到
    virtual bool Wait(uint64_t timeout) = 0;
};
```

实现上述接口并设置给服务发现接口使用
```c++
bool SetDataNotifyFactory(ConsumerApi* consumer, DataNotifyFactory factory);
```

### 编译&执行

具体示例参见`example/spp/mt_echo_consumer.cpp`。可配合非线程版本的服务注册示例`example/spp/echo_provider.cpp`使用

1. 先参照polaris sdk[编译文档](Building.md) 编译打包polaris c++ sdk。并将polaris_cpp_sdk.tar.gz解压到目录。

2. 下载spp代码，编译打包，并解压到目录。

3. 修改`example/spp/makefile`文件中前4行，指定到上述spp目录和polaris sdk目录。

4. 执行`make`编译出`mt_echo_consumer`

5. 参考[快速入门](QuickStart.md)，使用`mt_echo_consumer`替代文档中的`echo_consumer`进行测试


### 性能数据

由于一个DataNotify对象需要支持被多个微线程等待，而spp微线程不支持多个微线程等待同一个fd。所以目前例子中使用mt_sleep定期检查条件是否满足的机制实现DataNotify。

这里针对mt_sleep不同参数在C1机型上单线程(2494.14 MHz)运行demo程序，测试在服务数据无法获取时，所有微线程不断执行mt_sleep，得到CPU率数据如下：

| mt_sleep | 并发任务 | CPU使用率 |
|----------|---------|----------|
| 1ms      | 100     | 1%       |
| 1ms      | 1000    | 10%      |
| 1ms      | 5000    | 100%     |
| 2ms      | 5000    | 100%     |
| 5ms      | 5000    | 46%      |
| 10ms     | 100     | 0.7%     |
| 10ms     | 500     | 3%       |
| 10ms     | 1000    | 6%       |
| 10ms     | 2000    | 13%      |
| 10ms     | 4000    | 27%      |
| 10ms     | 5000    | 33%      |

由于首次获取有网络请求，建议mt_sleep的参数**默认为10ms**。
在mt_sleep参数为10时，**单核**同时有5000个微线程在mt_sleep这里不断被唤醒的情况下，**CPU占用为33%**。

## 服务注册

服务注册和心跳上报目前不支持微线程。
服务注册可在程序启动时调用阻塞接口进行注册。也可以直接在OSS添加被调端
