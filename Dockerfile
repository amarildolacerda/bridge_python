FROM espressif/esp-matter:latest

# Instalar GN e Ninja
RUN apt-get update && \
    apt-get install -y gn ninja-build && \
    apt-get clean

# Verificar instalação
RUN gn --version && ninja --version