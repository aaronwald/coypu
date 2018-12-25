FROM python:3
RUN pip install flask
RUN pip install kafka-python
RUN pip install websocket-client
COPY coypu_kafka.py .
CMD python3 coypu_kafka.py
