from pykafka import KafkaClient
client = KafkaClient(hosts="172.19.0.4:9092")
print client.topics
topic = client.topics['test']
with topic.get_sync_producer() as producer:
    for i in range(4):
        producer.produce('test message ' + str(i ** 2))


consumer = topic.get_simple_consumer()
for message in consumer:
    if message is not None:
        print message.offset, message.value
        break
    
