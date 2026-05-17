# Dentro do container
apt-get update
apt-get install -y gn ninja-build

# Verificar instalação
gn --version
ninja --version