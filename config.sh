#!/bin/bash
# config.sh - Versão que detecta ambiente (container ou host)

# Cores
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo "🔍 Detectando ambiente..."

# Verificar se está dentro do container espressif/esp-matter
if [ -f /.dockerenv ] || [ -d /opt/espressif/esp-idf ]; then
    echo -e "${GREEN}✅ Ambiente CONTAINER detectado${NC}"
    
    # Usar paths do container
    export IDF_PATH="/opt/espressif/esp-idf"
    export ESP_MATTER_PATH="/opt/espressif/esp-matter"
    
    # Já está configurado, apenas verificar
    if [ -f "$IDF_PATH/export.sh" ]; then
        echo "📦 Carregando ESP-IDF do container..."
        source "$IDF_PATH/export.sh" 2>/dev/null
    fi
    
else
    echo -e "${YELLOW}⚠️  Ambiente HOST detectado${NC}"
    
    # Configurar para ambiente host (fora do container)
    export IDF_PATH="${HOME}/tools/esp-idf"
    export ESP_MATTER_PATH="${HOME}/tools/esp-matter"
    
    if [ -f "$IDF_PATH/export.sh" ]; then
        echo "📦 Carregando ESP-IDF do host..."
        source "$IDF_PATH/export.sh"
    else
        echo -e "${RED}❌ ESP-IDF não encontrado em: $IDF_PATH${NC}"
        return 1
    fi
fi

# Verificar se funcionou
if command -v idf.py &> /dev/null; then
    echo -e "${GREEN}✅ Ambiente pronto!${NC}"
    echo "   IDF_PATH: $IDF_PATH"
    echo "   Versão: $(idf.py --version 2>/dev/null | head -1)"
else
    echo -e "${RED}❌ Falha na configuração${NC}"
    return 1
fi