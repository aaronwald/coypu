FROM ubuntu:18.04
COPY --from=coypu_llvm /usr/local/lib/libc++.so.1 /usr/local/lib
COPY --from=coypu_llvm /usr/local/lib/libc++abi.so.1 /usr/local/lib
COPY --from=coypu_llvm /usr/lib/libprotobuf.so.17 /usr/lib
COPY --from=coypu_llvm /usr/local/lib/libnghttp2.so.14 /usr/local/lib
COPY --from=coypu_llvm /usr/lib/x86_64-linux-gnu/libbpf.so.0 /usr/lib/x86_64-linux-gnu
WORKDIR /opt/coypu
COPY sh/entrypoint.sh .
COPY config/docker.yaml .
RUN apt-get update && apt-get install -y libnuma-dev libssl-dev libunwind-dev libyaml-dev ca-certificates
COPY src/rust-lib/target/debug/libcoypurust.so .
COPY build/coypu .

ENTRYPOINT ["/opt/coypu/entrypoint.sh"]
CMD ["/opt/coypu/coypu", "/opt/coypu/docker.yaml"]
