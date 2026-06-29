# Forçar Atualização via Git Pull — Bridge Python HA Add-on

## Resumo

Adicionar um botão "Force Update" no Home Assistant (via MQTT Discovery) que executa `git pull` no diretório fonte do add-on bridge-python e reinicia automaticamente para aplicar as alterações.

## Arquitetura

```
Botão HA (MQTT button platform)
       ↓ publish "PRESS" em command_topic
Bridge (MQTT subscription)
       ↓
cd /addons/esp32_bridge_python && git pull --ff-only
       ↓ sucesso?
   Sim → os._exit(0) → Supervisor watchdog reinicia o addon
   Não → publica erro via MQTT
```

## Mudanças por arquivo

### `config.yaml`
- Adicionar `map: ["addons:rw"]` para montar o diretório de addons no container
- Adicionar `watchdog: true` para reinício automático se o processo morrer

### `Dockerfile`
- Instalar `git` via apt
- Remover `COPY . .` (remove cópia do código fonte na imagem)
- Manter instalação de `requirements.txt`
- Copiar apenas `run.sh`
- WORKDIR continua `/app` para instalação de deps, mas runtime será no path montado

### `install_addon.sh`
- **Não** excluir `.git/` do rsync (remover a exclusão). O add-on precisa ser um repo git funcional.
- Garantir que `.gitattributes`, `.gitignore`, `.gitmodules` também sejam copiados
- Opcionalmente, trocar rsync por `git clone --depth 1`

### `run.sh`
- Detectar se o add-on está rodando com path montado (`/addons/esp32_bridge_python/app/main.py` existe)
- Se sim, exportar `BRIDGE_SRC_DIR=/addons/esp32_bridge_python` e executar Python de lá
- Se não (fallback), usar `/app`
- Cria diretório de dados persistido

### `app/config.py`
- Adicionar `Settings.addon_slug: str = "esp32_bridge_python"` (slug do add-on para montagem)

### `app/mqtt_discovery.py`
- Adicionar entidade fixa do bridge para o botão "Force Update":
  - Plataforma MQTT: `button`
  - `unique_id`: `esp32_bridge_force_update`
  - `device_class`: `update`
  - `icon`: `mdi:cloud-download`
  - `command_topic`: `esp32-bridge/force_update/set`
  - `payload_press`: `"PRESS"`
  - Agrupado no device do bridge (`identifiers: ["esp32_bridge_host"]`)
- Publicar config discovery no start
- Remover config discovery no stop (retain = "")

### `app/http_api.py`
- Nova rota `POST /api/gateway/git-pull`:
  - Body opcional: `{"path": "/custom/path"}` (default: auto-detectado)
  - Executa `git fetch --all && git log HEAD..origin/main --oneline` para preview
  - Se houver commits: `git pull --ff-only origin main`
  - Loga stdout/stderr do git
  - Retorna `{"status": "ok", "updated": true/false, "commits": [...], "output": "..."}`
  - Se faltar git no sistema: `{"status": "error", "message": "git not found"}`
  - Se não for repo git: `{"status": "error", "message": "not a git repository"}`
- Segurança: validar que o path está dentro de `/addons/` (evitar git pull arbitrário)

### `app/main.py`
- Inscrever no tópico MQTT `esp32-bridge/force_update/set` no startup
- Handler: recebe `"PRESS"` → executa git pull (mesma lógica da API) → publica resultado em `esp32-bridge/force_update/result`
- Se sucesso com mudanças: agendar `os._exit(0)` em 1s (watchdog reinicia)
- Se sucesso sem mudanças: publicar `{"status": "already_updated"}` sem restart
- Se erro: publicar `{"status": "error", "message": "..."}` sem restart

### `app/web/dashboard.html`
- Adicionar opção no menu admin: "Forçar Atualização (git pull)" com confirmação
- Exibir resultado (sucesso/erro, quantidade de commits)
- Indicar se restart será feito

## Detecção do path do repo

Ordem de resolução do `BRIDGE_SRC_DIR`:

1. Variável de ambiente `BRIDGE_SRC_DIR` (setada pelo `run.sh`)
2. Auto-detect: procurar por `app/main.py` em:
   - `/addons/esp32_bridge_python/`
   - `/data/bridge_python/`
   - `/app/` (baked-in, sem git)
3. Se nada encontrado, usar `/app` e desabilitar o botão

## Fluxo de restart

HA Supervisor com `watchdog: true`:
1. Botão pressionado → git pull com mudanças → `os._exit(0)`
2. Supervisor detecta processo morto → reinicia container
3. Container starta com código novo do volume montado
4. Se não houver watchdog, o `config.yaml` tem `startup: application` que já reinicia com o HA

## Considerações

- `.git/` no add-on: aumenta o tamanho da instalação (~2-5 MB). Aceitável para funcionalidade.
- Se o add-on for instalado sem `.git/` (ex: install manual), o botão fica desabilitado e retorna erro "not a git repository".
- git pull usa `--ff-only` para evitar merge conflicts. Se houver divergência local, o pull falha e o usuário precisa resolver manualmente.
- O Dockerfile não copia mais o código fonte. O add-on DEPENDE do mount `addons:rw` para funcionar. O fallback `/app` existe apenas para diagnóstico.
- A versão (FW_VERSION) deve ser a mesma da tag atual (conforme regras do projeto).

## Testes

- Testar que o botão MQTT aparece no discovery do HA
- Testar git pull em repo simulado (com/sem mudanças, com erro)
- Testar que watchdog reinicia após exit
- Testar fallback sem mount `/addons`
- Testar install_addon.sh com `.git/` incluso
