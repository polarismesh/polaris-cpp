# 测试说明
[TOC]

## 单元测试

## 集成测试

集成测试目录 /test/integration

执行集成测试必须设置两个环境变量
- POLARIS_SERVER 测试服务器地址，只支持单个服务器
- POLARIS_USER 测试用户名，用来设置所创建的资源的Owner

```bash
export POLARIS_SERVER=127.0.0.1
export POLARIS_USER=username
```

用例执行方法：
```bash
# 执行全部集成测试
make integration

# 执行单个文件里的单元测试，以common_test为例
make test/integration/common_test

# 执行某个文件里的某个测试用例，以common_test里的CommonTest.TestHttpClient为例
make test/integration/common_test TestCase=CommonTest.TestHttpClient
```

## 性能测试
