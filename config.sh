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
    
    # 🔥 Garantir que GN está no PATH
    export PATH="/usr/local/bin:$PATH"
    
    # Verificar GN
    if command -v gn &> /dev/null; then
        echo "   GN: $(gn --version 2>/dev/null | head -1)"
    else
        echo -e "${YELLOW}⚠️  GN não encontrado, instalando...${NC}"
        apt-get update -qq && apt-get install -y -qq gn ninja-build 2>/dev/null
    fi
    
    # Já está configurado, apenas verificar
    if [ -f "$IDF_PATH/export.sh" ]; then
        echo "📦 Carregando ESP-IDF do container..."
        source "$IDF_PATH/export.sh" 2>/dev/null
    fi

else
    echo -e "${YELLOW}⚠️  Ambiente HOST detectado${NC}"
    
    # Configurar para ambiente host (fora do container)
    export IDF_PATH="${HOME}/.espressif/v5.5.4/esp-idf"
    export RMAKER_PATH="${HOME}/esp/esp-rainmaker"
    
    if [ -f "$IDF_PATH/export.sh" ]; then
        echo "📦 Carregando ESP-IDF do host..."
        source "$IDF_PATH/export.sh"
    else
        echo -e "${RED}❌ ESP-IDF não encontrado em: $IDF_PATH${NC}"
        return 1
    fi
fi

# 🔥 Ativar ccache para acelerar rebuilds
export IDF_CCACHE_ENABLE=1
if command -v ccache &> /dev/null; then
    echo "   ccache: $(ccache --version | head -1)"
fi

# 🔥 Criar symlink para GN se necessário
if [ -f /usr/local/bin/gn ] && [ ! -f /usr/bin/gn ]; then
    ln -sf /usr/local/bin/gn /usr/bin/gn 2>/dev/null
fi

# Verificar se funcionou
if command -v idf.py &> /dev/null; then
    echo -e "${GREEN}✅ Ambiente pronto!${NC}"
    echo "   IDF_PATH: $IDF_PATH"
    echo "   Versão IDF: $(idf.py --version 2>/dev/null | head -1)"
    echo "   GN: $(gn --version 2>/dev/null | head -1)"
    echo "   Ninja: $(ninja --version 2>/dev/null | head -1)"
else
    echo -e "${RED}❌ Falha na configuração${NC}"
    return 1
fi