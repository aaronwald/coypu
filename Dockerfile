FROM coypu_llvm
WORKDIR /opt/coypu
COPY sh/entrypoint.sh .
COPY config/docker.yaml .
COPY src/rust-lib/target/debug/libcoypurust.so .
COPY build/coypu .
ENTRYPOINT ["/opt/coypu/entrypoint.sh"]
CMD ["/opt/coypu/coypu", "/opt/coypu/docker.yaml"]
