
#!/bin/bash

# Prática 5 - Rotina executada pelo ExecStop do systemd

# Registra no journal que foi solicitada a parada do serviço
logger -t pratica5-blink "Parada do servico solicitada."

exit 0
