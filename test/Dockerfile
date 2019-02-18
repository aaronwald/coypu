FROM python:latest
COPY requirements.txt .
RUN pip install -r requirements.txt
RUN mkdir /app
COPY gdax.py /app
WORKDIR /app
CMD ["/app/gdax.py"]
