FROM espressif/esp-matter:latest

# Instalar GN e Ninja
RUN apt-get update && \
    apt-get install -y gn ninja-build && \
    apt-get clean && \
    curl -fsSL https://opencode.ai/install | bash && \
    curl -fsSL https://code-server.dev/install.sh 

# Verificar instalação
RUN gn --version && ninja --version 
