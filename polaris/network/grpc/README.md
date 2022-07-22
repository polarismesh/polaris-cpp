本文件夹包含基于HTTP2的简化GRPC客户端实现


Grpc Client封装一个Http2 Client：
创建一个Grpc Client之后可以指定host:port来创建一个Http2 Client
也可以通过传入新的host:port来进行切换http2 client

Grpc Client上可以创建Grpc Stream，Grpc Stream封装了Http2 Stream。
同时，Grpc Request继承了Grpc Stream。
Grpc Stream对应原生Grpc的三种Stream RPC模式
Grpc Request对应Grpc Unary RPC模式

使用完成Grpc Client后可以通过传入end_stream参数和CloseLocal来释放
释放完成后不能再调用任何主动接口，但回调可能继续执行

