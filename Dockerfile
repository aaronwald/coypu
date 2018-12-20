FROM coypu_llvm
WORKDIR /opt/coypu
COPY build/coypu .
COPY config/docker.yaml .
COPY src/rust-lib/target/debug/libcoypurust.so .
COPY sh/entrypoint.sh .
ENTRYPOINT ["/opt/coypu/entrypoint.sh"]



