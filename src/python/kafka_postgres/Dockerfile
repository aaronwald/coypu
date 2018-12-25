FROM python:3
COPY requirements.txt .
RUN pip install -U pip
RUN pip install -r requirements.txt
COPY kafka_postgres.py .
CMD ["python3","-u","kafka_postgres.py"]
