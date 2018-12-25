#!/usr/bin/env python3

from kafka import KafkaConsumer
import psycopg2

if __name__ == "__main__":
    consumer = KafkaConsumer(bootstrap_servers="kafka:9092", api_version=(0,10))
    assert consumer
    consumer.subscribe(['trades'])
    conn = psycopg2.connect("dbname=trades user=postgres password=docker host=postgres")
    conn.set_session(autocommit=True)
    cur = conn.cursor()
    
    for doc in consumer:
        l = str(doc.value.decode("utf-8")).split(' ')
        product = l[1]
        last_px = l[3]
        trade_id = l[4]
        last_size = l[5]
        
        if product == "ZRX-USD":
            print (l)
            sql_command = "INSERT INTO last_trade VALUES ('%s', '%s', %s, %s)" % (trade_id, product, last_px, last_size)
            try:
                cur.execute(sql_command)
            except Exception as err:
                print(err)
                

    print("Shutting down....")


