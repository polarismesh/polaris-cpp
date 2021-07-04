# 如果在git仓库中获取revision
ifeq ($(shell git rev-parse --is-inside-work-tree), true)
	REVISION = $(shell git rev-parse --short HEAD)
else
	REVISION = NO_REVISION
endif