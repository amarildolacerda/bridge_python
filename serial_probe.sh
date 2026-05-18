#!/bin/bash

# ============================================================================
# serial_probe.sh - Diagnóstico e correção de porta serial para ESP32
# ============================================================================
# Uso: 
#   ./serial_probe.sh check  - Executa diagnóstico completo
#   ./serial_probe.sh start  - Tenta corrigir e ativar a porta serial
#   ./serial_probe.sh fix    - Força correção agressiva
#   ./serial_probe.sh monitor - Monitora a porta serial
# ============================================================================

# Cores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configurações
VID_PID="10c4:ea60"
DRIVER="cp210x"

# Funções
print_info() { echo -e "${BLUE}ℹ️  $1${NC}"; }
print_success() { echo -e "${GREEN}✅ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠️  $1${NC}"; }
print_error() { echo -e "${RED}❌ $1${NC}"; }

# ============================================================================
# DIAGNÓSTICO COMPLETO
# ============================================================================
diagnostic() {
    echo ""
    echo "================================================================================"
    echo -e "${BLUE}🔍 DIAGNÓSTICO DE PORTA SERIAL${NC}"
    echo "================================================================================"
    echo ""
    
    # 1. Verificar USB com lsusb
    echo -e "${BLUE}1. Dispositivos USB detectados:${NC}"
    if command -v lsusb &> /dev/null; then
        lsusb | grep -E "(Silicon|CP210|10c4:ea60|1a86:7523)"
        if [ $? -ne 0 ]; then
            print_warning "Nenhum dispositivo ESP32 encontrado no lsusb"
        fi
    else
        print_warning "lsusb não disponível"
    fi
    echo ""
    
    # 2. Verificar portas seriais
    echo -e "${BLUE}2. Portas seriais disponíveis:${NC}"
    PORTS=$(ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
    if [ -z "$PORTS" ]; then
        print_error "Nenhuma porta serial encontrada!"
    else
        echo "$PORTS"
    fi
    echo ""
    
    # 3. Verificar drivers carregados
    echo -e "${BLUE}3. Drivers seriais carregados:${NC}"
    LOADED=$(lsmod | grep -E "(cp210x|ftdi_sio|pl2303|ch341|usbserial)")
    if [ -z "$LOADED" ]; then
        print_warning "Nenhum driver serial carregado"
    else
        echo "$LOADED"
    fi
    echo ""
    
    # 4. Verificar logs do kernel
    echo -e "${BLUE}4. Últimas mensagens do kernel sobre USB/serial:${NC}"
    dmesg | grep -E "(ttyUSB|ttyACM|cp210|usb.*serial)" | tail -10
    echo ""
    
    # 5. Verificar permissões
    echo -e "${BLUE}5. Permissões do usuário:${NC}"
    echo "Usuário: $(whoami)"
    echo "Grupos: $(groups)"
    if [ -e /dev/ttyUSB0 ]; then
        ls -la /dev/ttyUSB0
    elif [ -e /dev/ttyACM0 ]; then
        ls -la /dev/ttyACM0
    fi
    echo ""
    
    # 6. Verificar usbipd (se disponível)
    echo -e "${BLUE}6. Status do usbipd (WSL):${NC}"
    if command -v usbip &> /dev/null; then
        sudo usbip list -r localhost 2>/dev/null | grep -A2 "$VID_PID"
    else
        print_warning "usbip não instalado"
    fi
    echo ""
    
    # 7. Verificar kernel
    echo -e "${BLUE}7. Informações do kernel:${NC}"
    echo "Versão: $(uname -r)"
    echo ""
    
    echo "================================================================================"
}

# ============================================================================
# CORREÇÃO E ATIVAÇÃO
# ============================================================================
activate() {
    echo ""
    echo "================================================================================"
    echo -e "${BLUE}🚀 ATIVANDO PORTA SERIAL${NC}"
    echo "================================================================================"
    echo ""
    
    # 1. Carregar drivers
    print_info "Carregando drivers seriais..."
    sudo modprobe usbserial
    sudo modprobe $DRIVER
    sudo modprobe ftdi_sio
    sudo modprobe pl2303
    sudo modprobe ch341
    
    # 2. Verificar se carregou
    if lsmod | grep -q "$DRIVER"; then
        print_success "Driver $DRIVER carregado"
    else
        print_error "Driver $DRIVER não carregou"
    fi
    echo ""
    
    # 3. Forçar detecção do dispositivo
    print_info "Forçando detecção do dispositivo $VID_PID..."
    echo "$VID_PID" | sudo tee /sys/bus/usb-serial/drivers/$DRIVER/new_id 2>/dev/null
    if [ $? -eq 0 ]; then
        print_success "Dispositivo registrado"
    else
        print_warning "Não foi possível registrar via sysfs"
    fi
    echo ""
    
    # 4. Acionar udev
    print_info "Atualizando regras udev..."
    sudo udevadm trigger
    sudo udevadm settle
    echo ""
    
    # 5. Aguardar e verificar
    sleep 2
    print_info "Verificando portas..."
    PORTS=$(ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
    if [ -z "$PORTS" ]; then
        print_error "Porta serial NÃO foi criada!"
        print_info "Tente a opção 'fix' para correção mais agressiva"
    else
        print_success "Porta serial encontrada:"
        echo "$PORTS"
        
        # Ajustar permissões
        if [ -e /dev/ttyUSB0 ]; then
            sudo chmod 666 /dev/ttyUSB0
            print_success "Permissões ajustadas: /dev/ttyUSB0"
        elif [ -e /dev/ttyACM0 ]; then
            sudo chmod 666 /dev/ttyACM0
            print_success "Permissões ajustadas: /dev/ttyACM0"
        fi
    fi
    echo ""
}

# ============================================================================
# CORREÇÃO AGRESSIVA
# ============================================================================
fix_aggressive() {
    echo ""
    echo "================================================================================"
    echo -e "${RED}🔥 CORREÇÃO AGRESSIVA${NC}"
    echo "================================================================================"
    echo ""
    
    print_warning "Esta opção irá reiniciar serviços e recarregar drivers"
    read -p "Continuar? (s/N): " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Ss]$ ]]; then
        echo "Cancelado."
        return
    fi
    
    # 1. Remover módulos
    print_info "Removendo módulos existentes..."
    sudo modprobe -r $DRIVER 2>/dev/null
    sudo modprobe -r usbserial 2>/dev/null
    sleep 1
    
    # 2. Carregar novamente
    print_info "Recarregando módulos..."
    sudo modprobe usbserial
    sudo modprobe $DRIVER
    
    # 3. Forçar bind via sysfs
    print_info "Forçando bind USB..."
    
    # Encontrar o bus do dispositivo
    BUS=$(lsusb | grep "$VID_PID" | awk '{print $2}')
    DEV=$(lsusb | grep "$VID_PID" | awk '{print $4}' | tr -d ':')
    
    if [ -n "$BUS" ] && [ -n "$DEV" ]; then
        print_info "Dispositivo encontrado: Bus $BUS Device $DEV"
        echo -n "1-${DEV}" | sudo tee /sys/bus/usb/drivers/usb/unbind 2>/dev/null
        sleep 1
        echo -n "1-${DEV}" | sudo tee /sys/bus/usb/drivers/usb/bind 2>/dev/null
    fi
    
    # 4. Recriar dispositivo serial
    print_info "Recriando dispositivo serial..."
    sudo rm -f /dev/ttyUSB0 /dev/ttyACM0 2>/dev/null
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    
    sleep 2
    
    # 5. Verificar resultado
    if [ -e /dev/ttyUSB0 ] || [ -e /dev/ttyACM0 ]; then
        print_success "Porta serial restaurada!"
        ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
    else
        print_error "Porta serial ainda não disponível"
        print_info "Execute 'check' para ver o diagnóstico"
    fi
}

# ============================================================================
# MONITORAR PORTA
# ============================================================================
monitor() {
    PORT=""
    
    # Encontrar porta
    if [ -e /dev/ttyUSB0 ]; then
        PORT="/dev/ttyUSB0"
    elif [ -e /dev/ttyACM0 ]; then
        PORT="/dev/ttyACM0"
    else
        print_error "Nenhuma porta serial encontrada"
        exit 1
    fi
    
    echo ""
    print_info "Monitorando $PORT (baudrate: 115200)"
    print_info "Pressione Ctrl + ] para sair"
    echo ""
    
    if command -v minicom &> /dev/null; then
        minicom -D $PORT -b 115200
    else
        sudo apt install -y minicom
        minicom -D $PORT -b 115200
    fi
}

# ============================================================================
# USO
# ============================================================================
usage() {
    echo "Uso: $0 {check|start|fix|monitor}"
    echo ""
    echo "Comandos:"
    echo "  check   - Executa diagnóstico completo"
    echo "  start   - Tenta ativar a porta serial (correção básica)"
    echo "  fix     - Correção agressiva (reinicia drivers)"
    echo "  monitor - Monitora a porta serial do ESP32"
    echo ""
    echo "Exemplos:"
    echo "  $0 check"
    echo "  $0 start"
    echo "  $0 monitor"
}

# ============================================================================
# MAIN
# ============================================================================
case "$1" in
    check)
        diagnostic
        ;;
    start)
        diagnostic
        activate
        ;;
    fix)
        fix_aggressive
        ;;
    monitor)
        monitor
        ;;
    *)
        usage
        exit 1
        ;;
esac