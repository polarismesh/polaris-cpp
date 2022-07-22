#!/bin/sh

if [[ $1 == "trpc" ]]
then
  bazel build polaris_api_trpc --cxxopt="--std=c++17"
  exit 0
fi

bazel build polaris_api
