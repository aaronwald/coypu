```
docker build -t coypu_kafka -q .
docker run  --rm -d --network coypu_net -t coypu_kafka 
```

```
docker run --rm --network coypu_net ches/kafka kafka-console-consumer.sh --topic trades --from-beginning --bootstrap-server kafka:9092
```
