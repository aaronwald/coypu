#!/usr/bin/env python3

from queue import Queue
from websocket import create_connection
from threading import Thread
from kafka import KafkaProducer

stopped = False
ws_queue = Queue()

def do_websocket():
    ws = create_connection("ws://coypu_server:8080/websocket")
    ws.send("Hello, World")

    # starts from zero each time
    while not stopped:
        result =  ws.recv()
        if result:
            ws_queue.put(result)

    ws.close()


if __name__ == "__main__":
    thread = Thread(target = do_websocket, args = [])
    thread.start()

    producer = KafkaProducer(bootstrap_servers="kafka:9092", api_version=(0,10))
    assert producer

    
    while 1:
        if not ws_queue.empty():
            doc = ws_queue.get(False)
            l = doc.split(' ')
            if l[0] == "Trade":
                print(l)
                product = l[1]
                last_px = l[3]
                trade_id = l[4]
                last_size = l[5]
                producer.send('trades', key=bytearray(product, 'utf-8'), value=bytearray(doc, 'utf-8'))
                
    stopped = True

    print("Shutting down....")


