#!/bin/bash
# reconfigure.sh - Reconfigura completamente o projeto

echo "========================================="
echo "  Reconfigurando projeto Matter Bridge"
echo "========================================="

# Carregar ambiente
source config.sh

# Limpar builds anteriores
echo "🧹 Limpando builds anteriores..."
idf.py fullclean

# Remover pasta build
rm -rf build

# Reconfigurar
echo "🔄 Reconfigurando..."
idf.py reconfigure

# Compilar
echo "🔨 Compilando..."
idf.py build

echo "✅ Done!"