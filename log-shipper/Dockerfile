FROM python:3.11-slim
RUN pip install --no-cache-dir boto3
WORKDIR /app
COPY log-shipper.py .
CMD ["python3", "-u", "log-shipper.py"]
