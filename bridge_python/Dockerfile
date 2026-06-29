FROM python:3.11-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

RUN chmod +x run.sh && mkdir -p /data/bridge_python

EXPOSE 80/tcp
EXPOSE 5000/udp

CMD ["sh", "-c", "mkdir -p /data/bridge_python && exec python3 -m app.main"]
