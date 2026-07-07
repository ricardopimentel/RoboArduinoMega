# Esquema de Ligação dos Pinos - Arduino Mega (Robô Tamagotchi)

Este documento descreve detalhadamente o esquema de ligação física de todos os componentes eletrônicos ao **Arduino Mega 2560** com base nas definições de firmware configuradas no arquivo [Robo.ino](file:///c:/Users/ifto/Documents/Projetos/Robo/Robo.ino).

---

## 1. Tabela Geral de Conexões

| Componente | Pino do Componente | Pino no Arduino Mega | Tipo de Sinal | Tensão de Operação | Observações / Notas |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Leitor RFID (MFRC522)** | 3.3V | **3.3V** | Alimentação | 3.3V | **ATENÇÃO:** Não ligar no 5V! |
| | GND | **GND** | Alimentação | 0V | Terra comum |
| | RST | **Pin 5** | Digital (Out) | 3.3V/5V | Reset do RFID |
| | MISO | **Pin 50** | SPI MISO | 3.3V/5V | Hardware SPI MISO |
| | MOSI | **Pin 51** | SPI MOSI | 3.3V/5V | Hardware SPI MOSI |
| | SCK | **Pin 52** | SPI SCK | 3.3V/5V | Hardware SPI SCK |
| | SDA (SS) | **Pin 53** | SPI SS | 3.3V/5V | Hardware SPI SS (Seleção do Chip) |
| **Display OLED (SSD1306)** | VCC | **5V** ou **3.3V** | Alimentação | 3.3V ~ 5V | Alimentação principal |
| | GND | **GND** | Alimentação | 0V | Terra comum |
| | SCL | **Pin 21 (SCL)** | I2C Clock | 5V | Barramento I2C (Compartilhado com MPU-6050) |
| | SDA | **Pin 20 (SDA)** | I2C Data | 5V | Barramento I2C (Compartilhado com MPU-6050) |
| **Giroscópio (MPU-6050)** | VCC | **5V** | Alimentação | 5V | Alimentação principal (Módulo GY-521) |
| | GND | **GND** | Alimentação | 0V | Terra comum |
| | SCL | **Pin 21 (SCL)** | I2C Clock | 5V | Barramento I2C (Compartilhado com OLED) |
| | SDA | **Pin 20 (SDA)** | I2C Data | 5V | Barramento I2C (Compartilhado com OLED) |
| **Sensor Ultrassônico (HC-SR04)**| VCC | **5V** | Alimentação | 5V | Alimentação principal |
| | GND | **GND** | Alimentação | 0V | Terra comum |
| | Trig | **Pin 9** | Digital (Out) | 5V | Gatilho do pulso ultrassônico |
| | Echo | **Pin 8** | Digital (In) | 5V | Retorno do pulso (Leitura de distância) |
| **Sensor de Temp/Umid (DHT11)** | VCC | **5V** ou **3.3V** | Alimentação | 3.3V ~ 5.5V | Alimentação principal |
| | GND | **GND** | Alimentação | 0V | Terra comum |
| | DATA (Out) | **Pin 2** | Digital (In) | 5V/3.3V | Linha de dados de leitura |
| **Sensor de Som (LM393/KY-038)** | G / GND | **GND** | Alimentação | 0V | Terra comum |
| | + / VCC | **5V** | Alimentação | 5V | Alimentação principal |
| | AO (Analog Out)| **Pin A1** | Analógico (In) | 0V ~ 5V | Medição de volume/amplitude de som |
| **Sensor de Luz (LDR)** | VCC | **5V** | Alimentação | 5V | Conectado em divisor de tensão com resistor 10kΩ |
| | OUT | **Pin A0** | Analógico (In) | 0V ~ 5V | Leitura de luminosidade |
| **Buzzer** | Positivo (+) | **Pin 7** | Digital (Out) | 5V | Emissão de bipes/sons de reações |
| | Negativo (-) | **GND** | Alimentação | 0V | Terra comum |
| **Botão (Menu/Interação)** | Terminal 1 | **Pin 4** | Digital (In) | 5V | Configurado como `INPUT_PULLUP` interno |
| | Terminal 2 | **GND** | Alimentação | 0V | Terra comum |

---

## 2. Detalhes de Conexão por Componente

### 2.1. Barramento SPI (Leitor RFID MFRC522)
O leitor RFID se comunica usando o protocolo SPI. No Arduino Mega, os pinos de SPI por hardware são fixos:
* **MISO**: Pino 50
* **MOSI**: Pino 51
* **SCK**: Pino 52
* **SS (SDA)**: Pino 53 (Pino de hardware Slave Select)
* **RST (Reset)**: Pino 5
> [!IMPORTANT]
> O módulo RFID MFRC522 opera estritamente a **3.3V**. Conectá-lo ao pino de 5V do Arduino Mega pode danificar permanentemente o sensor.

---

### 2.2. Barramento I2C (Display OLED SSD1306 e Giroscópio MPU-6050)
Ambos os dispositivos compartilham o mesmo barramento I2C do Arduino Mega:
* **SDA**: Pino 20
* **SCL**: Pino 21
* **Ligação em paralelo**: Conecte os pinos SDA do Display e do MPU-6050 juntos e ligue ao pino 20 do Arduino Mega. Faça o mesmo para os pinos SCL ligando ao pino 21.

---

### 2.3. Sensor Ultrassônico (HC-SR04)
Utilizado para medir a proximidade de objetos e interagir com o Tamagotchi:
* **Trig**: Pino 9 (Envia pulso)
* **Echo**: Pino 8 (Recebe pulso)
> [!NOTE]
> O pino `Echo` foi configurado no pino 8 para desocupar o pino 10, garantindo o funcionamento livre do barramento SPI e outras conexões de hardware.

---

### 2.4. Sensor de Luz (LDR)
Mede a luz ambiente para determinar se o robô deve "dormir" ou "acordar".
* Requer um circuito divisor de tensão simples com um **resistor de 10kΩ**.
* **Esquema do divisor**:
  ```text
  5V ---- [ LDR ] ----*---- [ Resistor 10kΩ ] ---- GND
                      |
                   Pino A0
  ```

---

### 2.5. Botão de Interação (Push Button)
* Conectado entre o **Pino 4** e o **GND**.
* Não necessita de resistor físico externo (pull-up), pois o firmware configura o pino com `INPUT_PULLUP` (ativa o resistor interno de pull-up do microcontrolador).

---

### 2.6. Buzzer
* O terminal positivo do buzzer (geralmente marcado com um `+` ou perna mais longa) é ligado no **Pino 7**.
* O terminal negativo é ligado diretamente ao **GND**.
* O firmware utiliza a função `tone()` neste pino para gerar alertas e músicas de interação.
