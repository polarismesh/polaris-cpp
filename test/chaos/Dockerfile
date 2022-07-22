FROM mirrors.tencent.com/tlinux/tlinux2.6:latest

LABEL maintainer="lambdaliu@tencent.com"

COPY ./chaos ./
COPY ./polaris.yaml ./
COPY ./chaos.yaml ./
COPY ./run.sh ./

CMD ["./run.sh"]
