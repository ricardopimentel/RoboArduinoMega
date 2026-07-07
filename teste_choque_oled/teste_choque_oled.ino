#include <Wire.h>
#include <U8g2lib.h>

const int inclinacaoPin = 16; // Sensor de Inclinação (GPIO 16)
const int buzzerPin = 19;     // Buzzer (GPIO 19)

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

bool inclinacaoDetectada = false;
unsigned long tempoUltimoMovimento = 0;
int contadorMovimentos = 0;
int ultimoEstadoInclinacao = HIGH;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(inclinacaoPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  
  Wire.begin(4, 5); // SDA = GPIO 4, SCL = GPIO 5
  u8g2.begin();
  
  ultimoEstadoInclinacao = digitalRead(inclinacaoPin);
  Serial.println("=== TESTE DE SENSOR DE INCLINACAO E OLED ===");
}

void loop() {
  int estadoAtual = digitalRead(inclinacaoPin);
  
  // Detecta mudança de estado (indica que a esfera interna rolou)
  if (estadoAtual != ultimoEstadoInclinacao) {
    if (!inclinacaoDetectada) {
      contadorMovimentos++;
      // Bipe curto no buzzer para confirmação imediata
      tone(buzzerPin, 1000, 50); 
    }
    inclinacaoDetectada = true;
    tempoUltimoMovimento = millis();
    Serial.print("INCLINACAO DETECTADA! Estado: ");
    Serial.println(estadoAtual);
    
    ultimoEstadoInclinacao = estadoAtual;
  }
  
  // Mantém a indicação na tela por 1.5 segundos após a última movimentação
  if (millis() - tempoUltimoMovimento > 1500) {
    inclinacaoDetectada = false;
  }
  
  // Desenha na tela OLED
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_profont12_tf);
    u8g2.drawStr(10, 15, "TESTE SENSOR DE INCLIN");
    u8g2.drawLine(0, 20, 128, 20);
    
    u8g2.setFont(u8g2_font_profont15_tf);
    if (inclinacaoDetectada) {
      u8g2.drawStr(15, 42, ">> MOVIMENTO! <<");
      u8g2.drawFrame(5, 25, 118, 25);
    } else {
      u8g2.drawStr(15, 42, "AGUARDANDO...");
    }
    
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.setCursor(10, 60);
    u8g2.print("Movimentos: ");
    u8g2.print(contadorMovimentos);
    u8g2.print(" (Pino: ");
    u8g2.print(estadoAtual);
    u8g2.print(")");
    
  } while (u8g2.nextPage());
  
  delay(10); // Pequena pausa para estabilidade do loop
}
