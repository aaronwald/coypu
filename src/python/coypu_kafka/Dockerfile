FROM python:3
COPY requirements.txt .
RUN pip install -U pip
RUN pip install -r requirements.txt
COPY coypu_kafka.py .
CMD ["python3","-u","coypu_kafka.py"]
	 