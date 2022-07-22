# 编译&引入

本文档提供如果编译并在自己的项目中引入Polaris CPP SDK方法

[TOC]

## 编译安装

### 下载源码

支持两种方式下载源码：
1. 使用git clone源码然后切到最新的tag
2. 直接在polaris cpp仓库的tags页面上下载最新的tag源码

### 编译打包

注：目前支持make, bazel方式编译。其他编译方式待支持，欢迎贡献。

#### make方式编译

``` bash
# 编译
make
# 测试
make test
# 打包，可添加参数package_name指定打包名，默认polaris_cpp_sdk
make package # package_name=polaris_cpp_sdk
```

执行`make package`后会在当前目录下生成一个`polaris_cpp_sdk.tar.gz`压缩文件。该文件的内容如下：

```
|-- include/polaris  # 头文件
|   |-- consumer.h provider.h limit.h config.h context.h log.h defs.h ...
|-- dlib             # 动态库
|   |-- libpolaris_api.so
`-- slib             # 静态库
    |-- libpolaris_api.a libprotobuf.a
```

其中`include/polaris/`为头文件目录。业务程序使用`#include "polaris/xxx.h"`这种方式包含头文件。

`dlib/`为动态库目录。`libpolaris_api.so`为polaris的动态库。注：该动态库已经链接了libprotobuf.a
使用动态库，在发布应用程序时需要将该动态库一起发布，并需要确保能搜到这些动态库。

`slib/`为静态库目录。用户使用静态编译时需要链接该目录下libpolaris_api.a和libprotobuf.a两个静态库。

#### 自定义PB3版本

目前SDK与北极星Server通信使用的PB3，如果用户使用了PB2，需要升级到PB3，PB3兼容PB2。
且当前为了支持c++98编译，默认集成了protobuf 3.5.1版本。用户可以根据需要使用自己的PB3库

在Makefile中找到protobuf相关路径定义，修改以下三个路径指向自己的PB3即可：
```
PROTOBUF_INC_DIR =  # 修改成自己的PB3头文件路径
PROTOBUF_LIB =      # 修改成自己的PB3静态库路径
PROTOC =            # 修改成自己的PB3可执行文件protoc路径
```

#### 兼容其他版本PB

有一些业务使用了其他的PB版本，例如PB2或者与北极星自带的PB3.5.1不同的版本。
如果同时链接两个版本的PB库，会导致符号冲突。
有一种方案可以隐藏北极星使用的PB库符号。该方案只支持业务链接北极星动态库时可用。

具体步骤如下：

1. 到北极星源码根目录下执行`rm -rf third_party/protobuf/build*` 删除编译出来的protobuf库

2. 修改北极星Makefile文件，protobuf的configure命令加上符号隐藏选项"CXXFLAGS=-fvisibility=hidden"。
如下是编译64位库时修改的地方：
```makefile
$(PROTOBUF_DIR)/build64/libprotobuf.a: $(PROTOBUF_DIR)/configure
	@echo "[PROTOBUF] Preparing protobuf 64bit lib and protoc"
	@cd $(PROTOBUF_DIR); ./configure --with-pic --disable-shared --enable-static "CXXFLAGS=-fvisibility=hidden"
```
注：编译32位时修改32位PB编译命令的`"CXXFLAGS=-m32"`为`"CXXFLAGS=-m32 -fvisibility=hidden"`即可

3. 在北极星根目录执行make clean 然后重新make即可

#### bazel方式编译

```
sh bazel_build.sh # 编译polaris_api
sh bazel_clean.sh # 编译清理
```
待补充:  
test用例的bazel编译待补充  

## 通过Makefile引入

### 静态库方式使用
```
g++ -I./polaris_cpp_sdk/include main.cpp -L./polaris_cpp_sdk/slib  -lpolaris_api -lprotobuf -pthread -lz -lrt -o main
```

### 动态库方式使用
```
g++ -I./polaris_cpp_sdk/include main.cpp -L./polaris_cpp_sdk/dlib -lpolaris_api -pthread -lz -lrt -o main
```

## 通过CMake引入

### 静态库方式使用
```
set(POLARIS_SDK_DIR /data/example/polaris_cpp_sdk)  # 需要修改polaris_cpp_sdk解压目录

include_directories(${POLARIS_SDK_DIR}/include)

link_directories(${POLARIS_SDK_DIR}/slib)

add_executable(main main.cpp)

target_link_libraries(main libpolaris_api.a libprotobuf.a pthread z rt)

```
### 动态库方式使用
```
set(POLARIS_SDK_DIR /data/example/polaris_cpp_sdk)  # 需要修改polaris_cpp_sdk解压目录

include_directories(${POLARIS_SDK_DIR}/include)

link_directories(${POLARIS_SDK_DIR}/dlib)

add_executable(main main.cpp)

target_link_libraries(main polaris_api pthread z rt)

```

## 通过Bazel引入

在WORKSPACE中添加依赖：
```
    git_repository(
        name = "polaris_cpp",
        remote = "http://git.woa.com/polaris/polaris-cpp.git",
        tag = "v0.13.1",  # 替换成需要依赖的版本
    )
```

在WORKSPACE中加载北极星依赖：
```
# 可以放到比较后面，这样如果有自定义其他依赖版本，则会优先使用其他自定义版本
load("//:polaris_deps.bzl", "polaris_deps")
polaris_deps()
```

在BUILD中设置编译目标添加依赖：
```
deps = [
        "@polaris_cpp//:polaris_api",
    ],
```

