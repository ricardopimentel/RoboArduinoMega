# Manual de Migração de Pinos e Hardware: Arduino Mega para ESP32-S3

Este guia detalha o processo de migração das conexões físicas (hardware) e configuração do ambiente para o robô Tamagotchi utilizando a placa **ESP32-S3**, integrado com sensores de obstáculo IR e choque.

---

## 1. Tabela de Conexão dos Pinos (De-Para)

A tabela abaixo mostra a correspondência exata de pinagem para o ESP32-S3:

| Componente | Nome do Pino | Conexão Arduino Mega | Conexão ESP32-S3 | Alimentação Recomendada |
| :--- | :--- | :--- | :--- | :--- |
| **Display OLED** | SDA | Pino 20 (SDA) | **GPIO 4** | 3.3V |
| | SCL | Pino 21 (SCL) | **GPIO 5** | 3.3V |
| **RFID MFRC522** | MISO | Pino 50 | **GPIO 13** | 3.3V |
| | MOSI | Pino 51 | **GPIO 11** | 3.3V |
| | SCK | Pino 52 | **GPIO 12** | 3.3V |
| | SDA (SS) | Pino 53 | **GPIO 10** | 3.3V |
| | RST | Pino 5 | **GPIO 14** | 3.3V |
| **Buzzer** | I/O | Pino 7 | **GPIO 18** | 3.3V (ou 5V se passivo) |
| **Botão de Interação** | I/O | Pino 4 | **GPIO 21** | GND (usa Pull-Up interno) |
| **Sensor de Luz (LDR)** | AO (Sinal) | Pino A0 | **GPIO 1** (ADC1_CH0) | 3.3V |
| **Sensor de Som** | AO (Sinal) | Pino A1 | **GPIO 2** (ADC1_CH1) | 3.3V |
| **Sensor DHT11/22** | DATA | Pino 6 | **GPIO 17** | 3.3V |
| **Detector de Obstáculos IR** | OUT (Sinal) | - | **GPIO 15** | 3.3V |
| **Sensor de Choque (Vibração)**| OUT (Sinal) | - | **GPIO 16** | 3.3V |

---

## 2. Ligações Elétricas e Simplificações (Proteção do ESP32)

### A. Sensores Digitais (Obstáculo IR e Choque/Vibração)
* **Detector de Obstáculos IR**: Substitui o antigo sensor ultrassônico HC-SR04. Ele opera em **3.3V** nativos do ESP32. Quando um objeto se aproxima dele, sua saída `OUT` vai para nível baixo (`LOW`), disparando a reação do robô.
* **Sensor de Choque / Inclinação**: Substitui o giroscópio MPU-6050 para detecção de movimento. Ele possui uma mola ou esfera interna. Quando a placa é movimentada, o sinal na saída varia (`HIGH`/`LOW`), ativando os olhos de **tontura** do robô.
* **Sem divisores de tensão**: Por operarem todos em 3.3V, não são necessários resistores ou divisores de tensão no circuito!

### B. Sensores Analógicos (LDR e Som)
* Conecte a alimentação do LDR e do Sensor de Som na linha de **3.3V** do ESP32-S3.
* O conversor analógico-digital (ADC) do ESP32-S3 lê entre 0V e 3.3V. O código já está pré-calibrado para a resolução de **12 bits** (valores de 0 a 4095).

---

## 3. Configuração do Ambiente no Arduino IDE

Para compilar o código na pasta [RoboESP32](file:///c:/Users/ifto/Documents/Projetos/Robo/RoboESP32/RoboESP32.ino):

1. **Instalar o suporte para ESP32**:
   * No Arduino IDE, acesse: `Arquivo -> Preferências` (ou `File -> Preferences`).
   * No campo **URLs Adicionais de Gerenciadores de Placas**, adicione:
     ```text
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   * Acesse `Ferramentas -> Placa -> Gerenciador de Placas` e instale o pacote **"esp32 by Espressif Systems"** (versão 2.x ou superior).

2. **Configurar as opções da Placa**:
   * Selecione a placa: **"ESP32S3 Dev Module"**.
   * Em **USB CDC On Boot**, defina como:
     * **`Enabled` (Ativado)** se estiver usando a porta USB Nativa (OTG).
     * **`Disabled` (Desativado)** se estiver usando a porta UART/COM padrão.

3. **Gravar na Placa**:
   * Se a IDE falhar na conexão (*timeout*), pressione e **segure** o botão **BOOT**, dê um **clique rápido** no botão **EN (Reset)**, solte o botão **BOOT** e tente carregar novamente.

---

## 4. Configuração de WiFi e Painel Web do Robô

Com os novos recursos implementados, o robô se conecta à rede local e roda um painel interativo.

### A. Configurar o Nome e Senha da sua Rede WiFi
Abra o código **[RoboESP32.ino](file:///c:/Users/ifto/Documents/Projetos/Robo/RoboESP32/RoboESP32.ino)** e edite as linhas abaixo no topo do código:
```cpp
const char* ssid = "NOME_DO_SEU_WIFI";
const char* password = "SENHA_DO_SEU_WIFI";
```

### B. Como acessar o Painel de Controle pelo Celular/Computador
1. Ao ligar, o robô tentará conectar ao seu WiFi por 5 segundos. 
2. Se conseguir, ele **exibirá o IP do robô na tela OLED** (ex: `192.168.1.15`).
3. No seu celular ou computador conectado na mesma rede WiFi, abra o navegador e digite o endereço IP na barra de endereços.
4. O painel web se abrirá em Dark Mode, exibindo a fome, saúde, temperatura, etc., com botões para alimentar, brincar e dar remédio remotamente!
