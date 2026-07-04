FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends git && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY run.sh .
COPY app/ /app/app/

RUN chmod +x run.sh && mkdir -p /data/home_bridge

EXPOSE 80/tcp
EXPOSE 5000/udp

CMD ["sh", "-c", "exec /app/run.sh"]
