#include <DHT.h>

#define DHTPIN 2       
#define DHTTYPE DHT11  
DHT dht(DHTPIN, DHTTYPE);

void setup() {
  Serial.begin(9600);
  Serial.println("--- Teste Unitario do Sensor DHT11 ---");
  dht.begin();
}

void loop() {
  float temp = dht.readTemperature();
  float umid = dht.readHumidity();

  if (isnan(temp) || isnan(umid)) {
    Serial.println("Erro ao ler dados do sensor DHT11! Verifique as conexoes (VCC, GND, DATA no Pino 2).");
  } else {
    Serial.print("Temperatura: ");
    Serial.print(temp);
    Serial.print(" *C | Umidade: ");
    Serial.print(umid);
    Serial.println(" %");
  }
  
  delay(2000); // Leitura recomendada a cada 2 segundos para o DHT11
}
