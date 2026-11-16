 
# Prática 5 - Checkpoint 1

# Aplicação de blink para inicialização automática via systemd

from gpiozero import LED
from time import sleep
from signal import signal, SIGTERM, SIGINT

# GPIO utilizada pelo LED (numeração BCM)
LED_PIN = 15

led = LED(LED_PIN)
executando = True

# Função para tratar sinais de encerramento
def encerrar(signum, frame):
    global executando
    executando = False


# Trata o sinal enviado ao parar o serviço
signal(SIGTERM, encerrar)

signal(SIGINT, encerrar)


try:
    print("Servico de blink iniciado.", flush=True)

    # Pisca o LED enquanto o serviço estiver ativo
    while executando:

        led.on()
        sleep(0.2)

        led.off()
        sleep(0.2)

finally:
    # Desliga o LED e libera a GPIO ao encerrar
    led.off()
    led.close()

    print("Servico de blink encerrado.", flush=True)
