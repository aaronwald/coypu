FROM python:latest
COPY requirements.txt .
RUN pip install -r requirements.txt
RUN mkdir /app
COPY gdaxkraken.py /app
WORKDIR /app
CMD ["/app/gdaxkraken.py"]
