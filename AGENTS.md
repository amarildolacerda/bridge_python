* observações
1. O comando `echo` é usado para imprimir texto na saída padrão (geralmente o terminal).
2. nao executa build automaticamente, apenas imprime a mensagem "build" no terminal.
3. se for fazer build, usar cached para evitar recompilar tudo do zero, o que pode economizar tempo e recursos.
4. O comando `echo` é útil para depuração e para fornecer feedback ao usuário durante
5. html deve ficar na pasta public, para ser servido corretamente pelo servidor web.
6. os codigos devem ter classes e arquivos dedicados por finalidade
7. Ponteiros `const char*` para memória interna do cJSON viram dangling após `cJSON_Delete()`. Copiar com `strncpy` para buffer local antes de deletar.
8. Service name UDP discovery (`"esp-bridge"`) precisa ser idêntico no cliente e no gateway, senão o discovery nunca responde.
9. Cliente deve ter retry de registro no `loop()`, não apenas uma chamada no `setup()`.
10. `httpd_accept_conn: error in accept (23)` = `ENFILE` — limite de sockets. Aumentar `CONFIG_LWIP_MAX_SOCKETS` no `sdkconfig` e configurar `max_open_sockets`, `keep_alive_enable`, `lru_purge_enable` no HTTP server.
11. `PROP_FLAG_READ` como flag de params RainMaker para sensores (só leitura).
12. Para enviar temperatura+umidade juntos no RainMaker, o device `TEMPERATURE_SENSOR` precisa criar manualmente o param `"Humidity"` extra, pois o `esp_rmaker_temp_sensor_device_create` padrão só cria `Temperature`.
13. Separar heartbeat (alive, sem MQTT) de dados (só envia HTTP quando o valor muda) para economizar orçamento MQTT do RainMaker e evitar `Out of MQTT Budget`.
14. Usar NVS (`nvs_set_blob`/`nvs_get_blob`) para persistir lista de devices bridged, restaurando no boot para evitar rediscovery na Alexa após reboot.
15. o identificar do serviço de troca de mensagem entre clients e bridge é " esp-bridge"
```sh

```