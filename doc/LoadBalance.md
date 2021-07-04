# 负载均衡

本文档提供Polaris C++ 负载均衡相关说明

[TOC]

## 服务发现使用

服务发现使用参考[用户指南-服务发现](UserGuide.md#服务发现)文档

## 负载均衡类型

在自己项目中编译并引入Polaris CPP SDK的方法参见[编译&引入](Building.md)

## 服务发现设置负载均衡算法

针对单次服务发现请求设置负载均衡算法
```c++
  /// @brief 设置负载均衡类型。可选，默认使用配置文件中设置的类型
  ///
  /// @param load_balance_type 负载均衡类型
  void GetOneInstanceRequest::SetLoadBalanceType(LoadBalanceType load_balance_type);
```
其中LoadBalanceType可取值如下：
- kLoadBalanceTypeWeightedRandom  // 权重随机
- kLoadBalanceTypeRingHash        // 一致性hash负载均衡
- kLoadBalanceTypeMaglevHash      // 一致性Hash: maglev算法
- kLoadBalanceTypeL5CstHash       // 兼容L5的一致性Hash
- kLoadBalanceTypeSimpleHash      // hash_key%总实例数 选择服务实例
- kLoadBalanceTypeDefaultConfig   // 使用全局配置的负载均衡算法，默认值


## 设置SDK默认负载均衡算法

在配置中设置SDK级别的默认负载均衡算法的方式如下：
```yaml
consumer:
  #描述:负载均衡相关配置      
  loadBalancer:
    #描述:负载均衡类型
    #范围:已注册的负载均衡插件名
    #默认值：权重随机负载均衡
    type: weightedRandom
```
其中type的可取值如下：
- weightedRandom: 随机权重算法
- ringHash: 一致性hash算法（ringHash)
- maglev: 一致性hash算法（maglev）
- l5cst: 与L5报错一致的一致性hash算法
- simpleHash: 简单hash算法，忽略权重，hash_key % 可用实例数量

## hash算法设置hash key

有两种方法可以设置hash key
1. 业务自己传入int64类型的hash key
```c++
  /// @brief 设置hash key，用于一致性哈希负载均衡算法选择服务实例。其他负载均衡算法不用设置
  void GetOneInstanceRequest::SetHashKey(uint64_t hash_key);
```

2. 业务传入hash string给SDK计算hash key
```c++
  /// @brief 设置 hash 字符串, sdk 会用 hash_string 算出一个 uint64_t
  /// 的哈希值用于一致性哈希负载均衡算法选择服务实例。其他负载均衡算法不用设置
  void GetOneInstanceRequest::SetHashString(const std::string& hash_string);
```