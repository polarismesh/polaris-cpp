#!/bin/sh

set -x

make clean
make

docker build . -t mirrors.tencent.com/polaris-cpp/chaos:$1
docker push mirrors.tencent.com/polaris-cpp/chaos:$1
