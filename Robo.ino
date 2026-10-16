#include <U8g2lib.h>
#include <Wire.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

// =========================================================================
// === CONFIGURAÇÃO DE TEMPOS DO TAMAGOTCHI (Edite aqui para alterar) ===
// =========================================================================
// Defina os tempos abaixo em milissegundos (ex: 1000 = 1 segundo)
const unsigned long INTERVALO_FOME = 99999;    // Fome decai a cada 2 segundos (rápido para testes)
const unsigned long INTERVALO_BRINCAR = 99999; // Diversão decai a cada 3 segundos (rápido para testes)
const unsigned long INTERVALO_SAUDE = 99999;   // Saúde decai a cada 4 segundos (rápido para testes)
const unsigned long INTERVALO_ALERTA = 60000; // Buzzer apita a cada 10 segundos se negligenciado (rápido para testes)
const int TEMPO_TELA_INFO = 4000;             // Tempo de permanência de cada tela de status (4s)
const unsigned long DURACAO_REACAO = 6000;    // Duração das animações de reação (comer, brincar, curar)

// --- CONFIGURAÇÃO DO TEMPO DE INCUBAÇÃO DO OVO ---
const unsigned int TEMPO_INCUBACAO_SEGUNDOS = 15; // Tempo para o ovo chocar em segundos (padrão: 900 para 15 minutos)

// --- CONFIGURAÇÃO DE LÓGICA DO SENSOR DE SOM ---
// Defina como true se o sensor envia nível LOW quando detecta barulho (Padrão LM393/KY-038)
// Defina como false se o sensor envia nível HIGH quando detecta barulho
const bool SENSOR_SOM_ATIVO_LOW = true; 
// =========================================================================

// --- CONFIGURAÇÕES DOS SENSORES ---
#define DHTPIN 2       
#define DHTTYPE DHT11  
DHT dht(DHTPIN, DHTTYPE);

const int trigPin = 9;
const int echoPin = 8; // Mudar de 10 para 8 para liberar pino SPI SS
const int ldrPin = A0;
const int botaoPin = 4;
const int buzzerPin = 7;
const int somAnalogPin = A1; // Sensor de Som (Analog Output)
const int LIMIAR_BRINCAR = 45;  // Volume intermediário (ex: voz alta, palmas médias)
const int LIMIAR_SUSTO = 70;   // Volume alto (ex: batida forte na mesa, grito)

// --- CONFIGURAÇÃO DO LEITOR RFID ---
#define SS_PIN 53
#define RST_PIN 5
MFRC522 rfid(SS_PIN, RST_PIN);

// --- VARIÁVEIS GLOBAIS ---
unsigned long tempoUltimaLeituraDHT = 0;
float temperatura = 26; // Começa com um valor normal
float umidade = 0;

enum EstadoTela {
  TELA_FACE,
  TELA_TEMP,
  TELA_UMID,
  TELA_PET_STATUS
};
EstadoTela telaAtual = TELA_FACE;
unsigned long tempoBotaoPressionado = 0;

// --- VARIÁVEIS DO BICHINHO VIRTUAL (PET / TAMAGOTCHI) ---
int fome = 80;       // 100 = saciado, 0 = faminto
int diversao = 75;   // 100 = feliz, 0 = entediado
int saude = 90;      // 100 = saudável, 0 = doente

unsigned long tempoUltimaFome = 0;
unsigned long tempoUltimoBrincar = 0;
unsigned long tempoUltimaSaude = 0;
unsigned long tempoUltimoAlerta = 0;

// Estados de reações
bool animacaoComendo = false;
bool animacaoBrincando = false;
bool animacaoCurando = false;
bool animacaoSusto = false;
bool animacaoAcariciado = false;
unsigned long tempoInicioReacao = 0;
unsigned long tempoInicioSusto = 0;
const unsigned long DURACAO_SUSTO = 2000; // O susto dura 2s


// Estado de doença por clima
bool estaDoente = false;
unsigned long tempoNoFrio = 0;
bool animacaoEspirro = false;
unsigned long tempoInicioEspirro = 0;
unsigned long tempoUltimoEspirro = 0;
unsigned long tempoProximoEspirro = 10000;

// UIDs para Auto-Aprendizado RFID
byte uidComida[4] = {0, 0, 0, 0};
byte uidBrincar[4] = {0, 0, 0, 0};
byte uidSaude[4] = {0, 0, 0, 0};
byte uidPet[4] = {0, 0, 0, 0};

// Estado do gatinho mini
bool petTagPresente = false;
float catMiniX = 105.0;
bool catMiniDirRight = true;
unsigned long tempoUltimoPasso = 0;

// --- SISTEMA DE NASCIMENTO E PERSONALIDADES ---
enum EstadoNascimento {
  ESTADO_OVO = 0,
  ESTADO_CHOCANDO = 1,
  ESTADO_NASCIDO = 2
};
EstadoNascimento estadoNascimento = ESTADO_OVO;
unsigned int segundosChocados = 0;
unsigned long tempoUltimoSegundoChocado = 0;

enum Personalidade {
  PERSONALIDADE_BASICA = 0,
  PERSONALIDADE_SARCASTICA = 1,
  PERSONALIDADE_SENSIVEL = 2
};
Personalidade personalidade = PERSONALIDADE_SARCASTICA; // Sempre começa como sarcástica
int pontosDeVinculo = 0;
unsigned long tempoUltimaVerificacaoVinculo = 0;

const int MPU_ADDR = 0x68; 
int16_t ax, ay, az; 
int16_t ax_ant, ay_ant, az_ant; 

// --- ESTRUTURAS E VARIÁVEIS DE ANIMAÇÃO DOS OLHOS ---
struct Particula {
  float x;
  float y;
  float velocidade;
  bool ativa;
};

struct ParticulazZz {
  float x;
  float y;
  float tamanho; 
  float fase;
  bool ativa;
};

// Posições e Tamanhos Atuais (para interpolação suave)
float eyeLx = 35.0, eyeLy = 32.0;
float eyeRx = 93.0, eyeRy = 32.0;
float eyeLw = 20.0, eyeLh = 30.0;
float eyeRw = 20.0, eyeRh = 30.0;
float eyeRadius = 6.0;

// Alvos de Interpolação
float targetLx = 35.0, targetLy = 32.0;
float targetRx = 93.0, targetRy = 32.0;
float targetLw = 20.0, targetLh = 30.0;
float targetRw = 20.0, targetRh = 30.0;
float targetRadius = 6.0;

// Configuração da suavização
const float INTERP_SPEED = 0.25;

// Temporizadores para piscadas e movimento ocioso (look around)
unsigned long tempoUltimaPiscada = 0;
unsigned long tempoProximaPiscada = 3000;
unsigned long tempoUltimoOlharOcioso = 0;
unsigned long tempoProximoOlharOcioso = 4000;
bool estaPiscando = false;
unsigned long duracaoPiscada = 0;

// Instâncias de partículas de efeito
Particula suor1 = {10.0, 0.0, 1.0, false};
Particula suor2 = {118.0, 0.0, 1.2, false};
Particula lagrimaL = {35.0, 32.0, 0.8, false};
Particula lagrimaR = {93.0, 32.0, 0.8, false};
ParticulazZz zZzVal = {80.0, 40.0, 1.0, 0.0, false};
unsigned long tempoUltimozZz = 0;

float anguloTontura = 0;


U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- FUNÇÕES AUXILIARES DE RFID ---
bool comparaUID(byte* a, byte* b) {
  for (byte i = 0; i < 4; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool eUIDZerado(byte* a) {
  return (a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0);
}

void copiaUID(byte* dest, byte* src) {
  for (byte i = 0; i < 4; i++) {
    dest[i] = src[i];
  }
}

void mostrarMensagemRegistro(const char* tipo) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(12, 18, "NOVO CARTAO!");
    u8g2.drawStr(12, 33, "REGISTRADO COMO:");
    u8g2.setFont(u8g2_font_profont12_tf);
    u8g2.drawStr(12, 52, tipo);
  } while ( u8g2.nextPage() );
  
  // Toca um bipe duplo alegre de confirmação
  tone(buzzerPin, 880, 80); delay(120);
  tone(buzzerPin, 1047, 180); delay(2000); // 2 segundos para o usuário ler a mensagem
}

// --- CONFIGURAÇÃO DE EEPROM PARA PERSISTÊNCIA ---
const byte EEPROM_MAGIC_BYTE = 0xA8; // Alterado para 0xA8 para forçar reset com a nova lógica de vínculo
const int EEPROM_ADDR_MAGIC = 0;
const int EEPROM_ADDR_FOME = 1;
const int EEPROM_ADDR_BRINCAR = 2;
const int EEPROM_ADDR_SAUDE = 3;
const int EEPROM_ADDR_UID_COMIDA = 4;
const int EEPROM_ADDR_UID_BRINCAR = 8;
const int EEPROM_ADDR_UID_SAUDE = 12;
const int EEPROM_ADDR_DOENTE = 16;
const int EEPROM_ADDR_UID_PET = 17;
const int EEPROM_ADDR_ESTADO_NASCIMENTO = 21;
const int EEPROM_ADDR_SEGUNDOS_CHOCADOS_MSB = 22;
const int EEPROM_ADDR_SEGUNDOS_CHOCADOS_LSB = 23;
const int EEPROM_ADDR_PERSONALIDADE = 24;
const int EEPROM_ADDR_PONTOS_DE_VINCULO = 25;

void salvarEstado() {
  EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_BYTE);
  EEPROM.update(EEPROM_ADDR_FOME, (byte)fome);
  EEPROM.update(EEPROM_ADDR_BRINCAR, (byte)diversao);
  EEPROM.update(EEPROM_ADDR_SAUDE, (byte)saude);
  EEPROM.update(EEPROM_ADDR_DOENTE, (byte)estaDoente);
  
  for (int i = 0; i < 4; i++) {
    EEPROM.update(EEPROM_ADDR_UID_COMIDA + i, uidComida[i]);
    EEPROM.update(EEPROM_ADDR_UID_BRINCAR + i, uidBrincar[i]);
    EEPROM.update(EEPROM_ADDR_UID_SAUDE + i, uidSaude[i]);
    EEPROM.update(EEPROM_ADDR_UID_PET + i, uidPet[i]);
  }
  
  EEPROM.update(EEPROM_ADDR_ESTADO_NASCIMENTO, (byte)estadoNascimento);
  EEPROM.update(EEPROM_ADDR_SEGUNDOS_CHOCADOS_MSB, (byte)(segundosChocados >> 8));
  EEPROM.update(EEPROM_ADDR_SEGUNDOS_CHOCADOS_LSB, (byte)(segundosChocados & 0xFF));
  EEPROM.update(EEPROM_ADDR_PERSONALIDADE, (byte)personalidade);
  EEPROM.update(EEPROM_ADDR_PONTOS_DE_VINCULO, (byte)pontosDeVinculo);
}

void carregarEstado() {
  byte magic = EEPROM.read(EEPROM_ADDR_MAGIC);
  if (magic == EEPROM_MAGIC_BYTE) {
    fome = EEPROM.read(EEPROM_ADDR_FOME);
    diversao = EEPROM.read(EEPROM_ADDR_BRINCAR);
    saude = EEPROM.read(EEPROM_ADDR_SAUDE);
    estaDoente = (bool)EEPROM.read(EEPROM_ADDR_DOENTE);
    
    for (int i = 0; i < 4; i++) {
      uidComida[i] = EEPROM.read(EEPROM_ADDR_UID_COMIDA + i);
      uidBrincar[i] = EEPROM.read(EEPROM_ADDR_UID_BRINCAR + i);
      uidSaude[i] = EEPROM.read(EEPROM_ADDR_UID_SAUDE + i);
      uidPet[i] = EEPROM.read(EEPROM_ADDR_UID_PET + i);
    }
    
    estadoNascimento = (EstadoNascimento)EEPROM.read(EEPROM_ADDR_ESTADO_NASCIMENTO);
    segundosChocados = (EEPROM.read(EEPROM_ADDR_SEGUNDOS_CHOCADOS_MSB) << 8) | EEPROM.read(EEPROM_ADDR_SEGUNDOS_CHOCADOS_LSB);
    personalidade = (Personalidade)EEPROM.read(EEPROM_ADDR_PERSONALIDADE);
    pontosDeVinculo = EEPROM.read(EEPROM_ADDR_PONTOS_DE_VINCULO);
    
    Serial.println(F("Estado carregado da EEPROM com sucesso!"));
    Serial.print(F("Estado Nascimento: "));
    Serial.println(estadoNascimento);
    Serial.print(F("Segundos Chocados: "));
    Serial.println(segundosChocados);
    Serial.print(F("Pontos de Vinculo: "));
    Serial.println(pontosDeVinculo);
    Serial.print(F("Personalidade do Pet: "));
    if (personalidade == PERSONALIDADE_BASICA) Serial.println(F("BASICA"));
    else if (personalidade == PERSONALIDADE_SARCASTICA) Serial.println(F("SARCASTICA"));
    else if (personalidade == PERSONALIDADE_SENSIVEL) Serial.println(F("SENSIVEL"));
  } else {
    Serial.println(F("EEPROM vazia ou antiga. Inicializando valores padrao de nascimento (OVO)..."));
    estaDoente = false;
    fome = 80;
    diversao = 75;
    saude = 90;
    for (int i = 0; i < 4; i++) {
      uidComida[i] = 0;
      uidBrincar[i] = 0;
      uidSaude[i] = 0;
      uidPet[i] = 0;
    }
    estadoNascimento = ESTADO_OVO;
    segundosChocados = 0;
    personalidade = PERSONALIDADE_SARCASTICA;
    pontosDeVinculo = 0;
    salvarEstado();
  }
}

// --- SISTEMA DE INCUBACAÇÃO DO OVO ---

void drawEgg(int x, int y, int crackStage, int wobbleOffset) {
  u8g2.setDrawColor(1);
  u8g2.drawFilledEllipse(x + wobbleOffset, y + 8, 16, 14); // base
  u8g2.drawFilledEllipse(x + wobbleOffset, y - 2, 12, 12); // topo
  
  if (crackStage >= 1) {
    u8g2.setDrawColor(0);
    // Rachadura no topo
    u8g2.drawLine(x + wobbleOffset - 2, y - 6, x + wobbleOffset + 2, y - 4);
    u8g2.drawLine(x + wobbleOffset + 2, y - 4, x + wobbleOffset - 1, y - 1);
    u8g2.drawLine(x + wobbleOffset - 1, y - 1, x + wobbleOffset + 3, y + 3);
  }
  if (crackStage >= 2) {
    // Estende a rachadura e adiciona uma ramificação esquerda
    u8g2.drawLine(x + wobbleOffset + 3, y + 3, x + wobbleOffset - 3, y + 7);
    u8g2.drawLine(x + wobbleOffset - 3, y + 7, x + wobbleOffset + 1, y + 11);
    
    u8g2.drawLine(x + wobbleOffset - 1, y - 1, x + wobbleOffset - 7, y + 1);
    u8g2.drawLine(x + wobbleOffset - 7, y + 1, x + wobbleOffset - 5, y + 5);
  }
  if (crackStage >= 3) {
    // Ramificação direita e outra na base
    u8g2.drawLine(x + wobbleOffset + 2, y - 4, x + wobbleOffset + 8, y - 2);
    u8g2.drawLine(x + wobbleOffset + 8, y - 2, x + wobbleOffset + 6, y + 2);
    
    u8g2.drawLine(x + wobbleOffset - 3, y + 7, x + wobbleOffset - 10, y + 8);
    u8g2.drawLine(x + wobbleOffset + 1, y + 11, x + wobbleOffset - 2, y + 15);
  }
  u8g2.setDrawColor(1); // restaura cor padrão
}

void nascerPet() {
  estadoNascimento = ESTADO_NASCIDO;
  
  // O pet sempre começa com a personalidade sarcástica no nascimento
  personalidade = PERSONALIDADE_SARCASTICA;
  pontosDeVinculo = 0;
  tempoUltimaVerificacaoVinculo = millis();
  
  // Reseta os atributos do bichinho para o nascimento
  fome = 80;
  diversao = 75;
  saude = 90;
  estaDoente = false;
  
  salvarEstado();

  // Imprime no Serial Monitor qual personalidade nasceu
  Serial.println(F("=== O PET NASCEU! ==="));
  Serial.println(F("Personalidade Inicial: SARCASTICA"));
  Serial.println(F("Trate-o bem para evoluir o vínculo!"));

  // Desenha uma mensagem de nascimento na tela por 4 segundos
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(12, 16, "O PET NASCEU!");
    u8g2.drawStr(12, 28, "O OVO RACHOU!");
    u8g2.drawStr(12, 44, "PERSONALIDADE:");
    u8g2.setFont(u8g2_font_profont12_tf);
    u8g2.drawStr(12, 58, "SARCASTICA");
  } while (u8g2.nextPage());

  // Melodia alegre especial de nascimento (Fanfarra)
  tone(buzzerPin, 523, 100); delay(120);
  tone(buzzerPin, 659, 100); delay(120);
  tone(buzzerPin, 784, 100); delay(120);
  tone(buzzerPin, 1047, 300); delay(350);
  tone(buzzerPin, 784, 100); delay(120);
  tone(buzzerPin, 1047, 400);
  
  delay(4000); // 4 segundos para o usuário ler a personalidade na tela
}

void processarFluxoIncubacao() {
  // Lógica de contagem de tempo para o estado de chocar
  if (estadoNascimento == ESTADO_CHOCANDO) {
    if (tempoUltimoSegundoChocado == 0) {
      tempoUltimoSegundoChocado = millis();
    }
    
    if (millis() - tempoUltimoSegundoChocado >= 1000) {
      segundosChocados++;
      tempoUltimoSegundoChocado = millis();
      
      // Salva progresso na EEPROM a cada 10 segundos para persistir após desligamento
      if (segundosChocados % 10 == 0) {
        salvarEstado();
      }
      
      // Ao atingir o tempo limite configurado, o pet nasce
      if (segundosChocados >= TEMPO_INCUBACAO_SEGUNDOS) {
        nascerPet();
        return; // Sai do fluxo de incubação já que nasceu
      }
    }
  }

  // Leitura do RFID para chocar o ovo
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (estadoNascimento == ESTADO_OVO) {
      estadoNascimento = ESTADO_CHOCANDO;
      segundosChocados = 0;
      tempoUltimoSegundoChocado = millis();
      salvarEstado();
      
      // Toque melódico alegre para início da incubação
      tone(buzzerPin, 523, 100); delay(120);
      tone(buzzerPin, 659, 100); delay(120);
      tone(buzzerPin, 784, 250);
    }
    rfid.PICC_HaltA();
  }

  // Renderização de tela do Ovo
  u8g2.firstPage();
  do {
    if (estadoNascimento == ESTADO_OVO) {
      // Ovo intacto respirando sutilmente
      int pulso = (int)(sin(millis() * 0.003) * 1.5);
      drawEgg(64, 20, 0, pulso);
      
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(24, 48, "OVO MISTERIOSO");
      
      // Texto piscando "APROXIME TAG NFC" no rodapé
      if ((millis() / 500) % 2 == 0) {
        u8g2.drawStr(18, 62, "APROXIME TAG NFC");
      }
    } 
    else if (estadoNascimento == ESTADO_CHOCANDO) {
      // Determina estágio de rachadura de forma proporcional ao tempo total
      int stage = 1;
      if (segundosChocados >= (TEMPO_INCUBACAO_SEGUNDOS * 2) / 3) stage = 3;      // Último terço do tempo
      else if (segundosChocados >= TEMPO_INCUBACAO_SEGUNDOS / 3) stage = 2; // Segundo terço do tempo
      
      // Tremores físicos no ovo
      int wobble = 0;
      if (stage == 3) {
        if (random(0, 10) < 4) wobble = random(-2, 3); // tremor forte
      } else if (stage == 2) {
        if (random(0, 15) < 2) wobble = random(-1, 2); // tremor leve
      }
      
      drawEgg(64, 18, stage, wobble);
      
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(34, 44, "CHOCANDO...");
      
      // Barra de progresso (de X=24 a X=104, Y=48)
      u8g2.drawFrame(24, 48, 80, 5);
      int progressWidth = map(segundosChocados, 0, TEMPO_INCUBACAO_SEGUNDOS, 0, 76);
      if (progressWidth > 76) progressWidth = 76;
      u8g2.drawBox(26, 50, progressWidth, 1);
      
      // Tempo restante (MM:SS)
      unsigned int tempoRestante = TEMPO_INCUBACAO_SEGUNDOS - segundosChocados;
      unsigned int minRestantes = tempoRestante / 60;
      unsigned int segRestantes = tempoRestante % 60;
      char tempoStr[10];
      sprintf(tempoStr, "%02d:%02d", minRestantes, segRestantes);
      u8g2.drawStr(48, 61, tempoStr);
    }
  } while (u8g2.nextPage());
  
  delay(35);
}

// --- CONTROLE DE EVOLUÇÃO E VÍNCULO DO PET ---
void atualizarVinculo(int alteracao) {
  int antigoVinculo = pontosDeVinculo;
  pontosDeVinculo = constrain(pontosDeVinculo + alteracao, 0, 120);
  
  if (pontosDeVinculo != antigoVinculo) {
    salvarEstado();
  }
  
  // Determina a personalidade baseada nos pontos de vínculo
  Personalidade antigaPers = personalidade;
  if (pontosDeVinculo < 40) {
    personalidade = PERSONALIDADE_SARCASTICA;
  } else if (pontosDeVinculo < 80) {
    personalidade = PERSONALIDADE_BASICA;
  } else {
    personalidade = PERSONALIDADE_SENSIVEL;
  }
  
  // Se a personalidade evoluiu ou regrediu
  if (personalidade != antigaPers) {
    salvarEstado(); // Salva a mudança na EEPROM
    
    Serial.println(F("============================================="));
    Serial.print(F("=== A PERSONALIDADE DO ROBÔ MUDOU ===\nNovo Vínculo: "));
    Serial.print(pontosDeVinculo);
    Serial.print(F(" | Nova Personalidade: "));
    
    // Desenha alerta de mudança na tela OLED por 3 segundos
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(12, 18, "PERSONALIDADE");
      if (personalidade > antigaPers) {
        u8g2.drawStr(12, 33, "EVOLUIU! :)");
      } else {
        u8g2.drawStr(12, 33, "REGREDIU... :(");
      }
      u8g2.setFont(u8g2_font_profont12_tf);
      
      if (personalidade == PERSONALIDADE_SARCASTICA) {
        Serial.println(F("SARCASTICA"));
        u8g2.drawStr(12, 52, "SARCASTICA");
      } else if (personalidade == PERSONALIDADE_BASICA) {
        Serial.println(F("BASICA"));
        u8g2.drawStr(12, 52, "BASICA (NORMAL)");
      } else if (personalidade == PERSONALIDADE_SENSIVEL) {
        Serial.println(F("SENSIVEL"));
        u8g2.drawStr(12, 52, "SENSIVEL (CARENTE)");
      }
    } while (u8g2.nextPage());
    Serial.println(F("============================================="));
    
    // Toca som correspondente
    if (personalidade > antigaPers) {
      // Evolução: melodia feliz subindo
      tone(buzzerPin, 523, 100); delay(120);
      tone(buzzerPin, 784, 100); delay(120);
      tone(buzzerPin, 1047, 300);
    } else {
      // Regressão: melodia triste descendo
      tone(buzzerPin, 880, 150); delay(200);
      tone(buzzerPin, 440, 350);
    }
    
    delay(3000); // Aguarda 3 segundos para leitura do usuário
  }
}

// --- FUNÇÕES DE REAÇÃO E INTERAÇÃO DO PET ---
void alimentarPet() {
  int ganho = 30;
  if (personalidade == PERSONALIDADE_SARCASTICA) ganho = 15;
  fome = fome + ganho;
  if (fome > 100) fome = 100;
  
  animacaoComendo = true;
  animacaoBrincando = false;
  animacaoCurando = false;
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  // Melodia do Buzzer adaptada para a personalidade
  if (personalidade == PERSONALIDADE_SARCASTICA) {
    // Sarcástica: som curto e desanimado
    tone(buzzerPin, 220, 100); delay(150);
  } else if (personalidade == PERSONALIDADE_SENSIVEL) {
    // Sensível: tom agudo e dócil
    tone(buzzerPin, 880, 50); delay(70);
    tone(buzzerPin, 1109, 50); delay(70);
    tone(buzzerPin, 1318, 80); delay(100);
  } else {
    // Melodia do Buzzer padrão: mastigação feliz
    tone(buzzerPin, 440, 60); delay(80);
    tone(buzzerPin, 554, 60); delay(80);
    tone(buzzerPin, 659, 100);
  }
  
  atualizarVinculo(3); // Ganha +3 pontos de vínculo por alimentar
  salvarEstado(); // Grava alteração de atributos na EEPROM
}

void brincarPet(bool tocarMelodia = true) {
  int ganho = 30;
  if (personalidade == PERSONALIDADE_SARCASTICA) ganho = 15;
  diversao = diversao + ganho;
  if (diversao > 100) diversao = 100;
  
  animacaoComendo = false;
  animacaoBrincando = true;
  animacaoCurando = false;
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  if (tocarMelodia) {
    if (personalidade == PERSONALIDADE_SARCASTICA) {
      // Sarcástica: bip rápido e seco
      tone(buzzerPin, 294, 100); delay(120);
    } else if (personalidade == PERSONALIDADE_SENSIVEL) {
      // Sensível: melodia dócil e fofa (caixinha de música)
      tone(buzzerPin, 1047, 50); delay(70);
      tone(buzzerPin, 1318, 50); delay(70);
      tone(buzzerPin, 1568, 50); delay(70);
      tone(buzzerPin, 2093, 100);
    } else {
      // Melodia do Buzzer padrão: Mario Power-up feliz
      tone(buzzerPin, 523, 50); delay(70);
      tone(buzzerPin, 659, 50); delay(70);
      tone(buzzerPin, 784, 50); delay(70);
      tone(buzzerPin, 1047, 100);
    }
  }
  
  atualizarVinculo(3); // Ganha +3 pontos de vínculo por brincar
  salvarEstado(); // Grava alteração de atributos na EEPROM
}

void curarPet() {
  saude = saude + 30;
  if (saude > 100) saude = 100;
  estaDoente = false; // Cura o resfriado
  
  animacaoComendo = false;
  animacaoBrincando = false;
  animacaoCurando = true;
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  // Melodia do Buzzer
  if (personalidade == PERSONALIDADE_SARCASTICA) {
    // Sarcástica
    tone(buzzerPin, 440, 150); delay(200);
  } else if (personalidade == PERSONALIDADE_SENSIVEL) {
    // Sensível
    tone(buzzerPin, 1760, 50); delay(80);
    tone(buzzerPin, 1760, 50); delay(80);
    tone(buzzerPin, 2218, 100);
  } else {
    tone(buzzerPin, 880, 80); delay(120);
    tone(buzzerPin, 880, 80); delay(120);
    tone(buzzerPin, 1109, 150);
  }
  
  atualizarVinculo(5); // Ganha +5 pontos de vínculo por curar
  salvarEstado(); // Grava alteração de atributos na EEPROM
}

void acariciarPet() {
  animacaoComendo = false;
  animacaoBrincando = false;
  animacaoCurando = false;
  animacaoAcariciado = true;
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  // Melodia do Buzzer
  if (personalidade == PERSONALIDADE_SARCASTICA) {
    // Sarcástica
    tone(buzzerPin, 330, 100); delay(120);
  } else if (personalidade == PERSONALIDADE_SENSIVEL) {
    // Sensível
    tone(buzzerPin, 1318, 80); delay(100);
    tone(buzzerPin, 1568, 80); delay(100);
    tone(buzzerPin, 1976, 120);
  } else {
    // Melodia do Buzzer padrão: miado feliz ou carinho
    tone(buzzerPin, 659, 100); delay(120);
    tone(buzzerPin, 784, 100); delay(120);
    tone(buzzerPin, 988, 150);
  }
  
  atualizarVinculo(4); // Ganha +4 pontos de vínculo por receber carinho
  salvarEstado(); // Grava alteração na EEPROM
}

// Função auxiliar para desenhar o olho de forma segura (previne crashes do u8g2.drawRBox e renderiza linha na piscada)
void drawEye(float x, float y, float w, float h, float r) {
  int ix = (int)(x - w / 2.0);
  int iy = (int)(y - h / 2.0);
  int iw = (int)w;
  int ih = (int)h;
  int ir = (int)r;
  
  if (ih <= 2) {
    // Desenha uma linha horizontal simples para representar o olho fechado
    u8g2.drawBox(ix, (int)y, iw, 1);
  } else {
    // Limita o raio para não dar bugs no drawRBox do u8g2
    int maxR = min(iw / 2, ih / 2);
    if (ir > maxR) ir = maxR;
    if (ir <= 0) {
      u8g2.drawBox(ix, iy, iw, ih);
    } else {
      u8g2.drawRBox(ix, iy, iw, ih, ir);
    }
  }
}

// Função auxiliar para desenhar um pequeno coração de pixel art (5x5 pixels)
void drawHeart(int x, int y) {
  u8g2.drawPixel(x - 1, y - 2);
  u8g2.drawPixel(x + 1, y - 2);
  u8g2.drawBox(x - 2, y - 1, 5, 2);
  u8g2.drawBox(x - 1, y + 1, 3, 1);
  u8g2.drawPixel(x, y + 2);
}

// --- DESENHADOR PROCEDURAL DE NÚMEROS 7-SEGMENTOS (Economiza Flash) ---
void draw7Segment(int x, int y, int value) {
  // Tamanho de cada dígito: largura 12, altura 24, espessura 2
  static const byte segs[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  if (value < 0 || value > 9) return;
  byte mask = segs[value];
  int w = 12;
  int h = 24;
  int t = 2; // espessura dos segmentos
  
  if (mask & 0x01) u8g2.drawBox(x + t, y, w - 2*t, t);                  // A (topo)
  if (mask & 0x02) u8g2.drawBox(x + w - t, y + t, t, h/2 - t);          // B (sup dir)
  if (mask & 0x04) u8g2.drawBox(x + w - t, y + h/2, t, h/2 - t);        // C (inf dir)
  if (mask & 0x08) u8g2.drawBox(x + t, y + h - t, w - 2*t, t);          // D (base)
  if (mask & 0x10) u8g2.drawBox(x, y + h/2, t, h/2 - t);                // E (inf esq)
  if (mask & 0x20) u8g2.drawBox(x, y + t, t, h/2 - t);                  // F (sup esq)
  if (mask & 0x40) u8g2.drawBox(x + t, y + h/2 - t/2, w - 2*t, t);      // G (meio)
}

void draw7SegmentNumber(int x, int y, int number) {
  if (number < 0) {
    u8g2.drawBox(x, y + 11, 6, 2);
    x += 8;
    number = -number;
  }
  if (number == 0) {
    draw7Segment(x, y, 0);
    return;
  }
  int digits[5];
  int count = 0;
  int temp = number;
  while (temp > 0) {
    digits[count++] = temp % 10;
    temp /= 10;
  }
  for (int i = count - 1; i >= 0; i--) {
    draw7Segment(x, y, digits[i]);
    x += 16; // 12 de largura + 4 de espaçamento
  }
}

int get7SegmentNumberWidth(int number) {
  if (number == 0) return 12;
  int count = 0;
  int temp = number < 0 ? -number : number;
  while (temp > 0) {
    count++;
    temp /= 10;
  }
  int w = count * 16 - 4;
  if (number < 0) w += 8;
  return w;
}

void setup() {
  Serial.begin(9600); // Inicializa comunicação Serial para depuração
  
  // Garante que o pino SS de hardware (53) esteja como saída para o SPI Master funcionar
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  pinMode(buzzerPin, OUTPUT); // Configura o pino do buzzer como saída
  
  SPI.begin(); // Inicializa barramento SPI
  rfid.PCD_Init(); // Inicializa o RFID MFRC522
  
  // Imprime informações de diagnóstico do RFID no Monitor Serial
  Serial.print(F("RFID MFRC522: "));
  rfid.PCD_DumpVersionToSerial();
  
  Wire.begin();
  Wire.setClock(400000); // Configura o clock do I2C para 400kHz (acelera a tela e a leitura dos botões)
  
  dht.begin();
  u8g2.begin();
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(botaoPin, INPUT_PULLUP); // Botão com resistor interno na diagonal
  // Sensor de som analógico (A1) não necessita de pinMode ou interrupção especial

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); 
  Wire.write(0); 
  Wire.endTransmission(true);

  // Se segurar o botão pressionado ao ligar o robô, limpa a EEPROM (Factory Reset)
  if (digitalRead(botaoPin) == LOW) {
    Serial.println(F("Limpando a EEPROM para reiniciar de fabrica..."));
    EEPROM.write(EEPROM_ADDR_MAGIC, 0); // Corrompe o magic byte
    // Toca som de aviso de reset: bipe longo
    tone(buzzerPin, 440, 500);
    delay(600);
  }
  
  // Carrega estado salvo do pet e UIDs do RFID (se houver)
  carregarEstado();
}

void loop() {
  // Se ainda não nasceu, roda o fluxo de incubação do ovo e desenha na tela
  if (estadoNascimento == ESTADO_OVO || estadoNascimento == ESTADO_CHOCANDO) {
    processarFluxoIncubacao();
    return;
  }

  // Leitura do sensor de luz (LDR) logo no início para uso no decaimento e animações
  int ldrValor = analogRead(ldrPin);
  int luzPorcento = map(ldrValor, 0, 1023, 100, 0);

  // --- 1. LÓGICA DO BOTÃO (Slideshow Automático com Debounce) ---
  bool botaoPressionado = (digitalRead(botaoPin) == LOW);
  static bool botaoUltimoEstado = HIGH;
  
  if (botaoPressionado && botaoUltimoEstado == HIGH) {
    if (telaAtual == TELA_FACE) {
      telaAtual = TELA_TEMP;
      tempoBotaoPressionado = millis();
    } else {
      // Se pressionar durante o ciclo, cancela e volta para o rosto imediatamente
      telaAtual = TELA_FACE;
    }
    delay(50); // Debounce
  }
  botaoUltimoEstado = botaoPressionado;

  // Transições Automáticas de Tempo
  if (telaAtual == TELA_TEMP) {
    if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
      telaAtual = TELA_UMID;
      tempoBotaoPressionado = millis(); // Reinicia o timer para a tela de umidade
    }
  } 
  else if (telaAtual == TELA_UMID) {
    if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
      telaAtual = TELA_PET_STATUS;
      tempoBotaoPressionado = millis(); // Reinicia o timer para a tela do pet
    }
  }
  else if (telaAtual == TELA_PET_STATUS) {
    if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
      telaAtual = TELA_FACE;
    }
  }

  // --- 2. DECAIMENTO DOS ATRIBUTOS DO PET ---
  if (petTagPresente) {
    diversao = 100; // Impede que sinta tédio/vontade de brincar enquanto estiver com o gatinho
  }
  // Fome e diversão só decaem se o pet estiver acordado (luzPorcento >= 10)
  if (luzPorcento >= 10) {
    unsigned long intervaloFomeAtual = INTERVALO_FOME;
    if (personalidade == PERSONALIDADE_SENSIVEL) {
      intervaloFomeAtual = (INTERVALO_FOME * 2) / 3; // 1.5x mais rápido
    }
    if (millis() - tempoUltimaFome >= intervaloFomeAtual) {
      fome = max(0, fome - 1);
      tempoUltimaFome = millis();
      salvarEstado();
    }
    
    unsigned long intervaloBrincarAtual = INTERVALO_BRINCAR;
    if (personalidade == PERSONALIDADE_SENSIVEL) {
      intervaloBrincarAtual = (INTERVALO_BRINCAR * 2) / 3; // 1.5x mais rápido
    } else if (personalidade == PERSONALIDADE_SARCASTICA) {
      intervaloBrincarAtual = (INTERVALO_BRINCAR * 5) / 3; // ~0.6x da velocidade (1.66x mais devagar)
    }
    if (millis() - tempoUltimoBrincar >= intervaloBrincarAtual) {
      if (!petTagPresente) {
        diversao = max(0, diversao - 1);
      }
      tempoUltimoBrincar = millis();
      salvarEstado();
    }
  } else {
    // Se o pet estiver dormindo, os timers de fome e diversão são mantidos atualizados para não decaírem logo ao acordar
    tempoUltimaFome = millis();
    tempoUltimoBrincar = millis();
  }
  
  unsigned long intervaloSaudeAtual = INTERVALO_SAUDE;
  if (personalidade == PERSONALIDADE_SENSIVEL) {
    intervaloSaudeAtual = INTERVALO_SAUDE / 2; // 2x mais rápido
  }
  if (millis() - tempoUltimaSaude >= intervaloSaudeAtual) {
    // Saúde decai se o pet estiver faminto E entediado, OU se estiver doente (estaDoente)
    int limiarFome = (personalidade == PERSONALIDADE_SENSIVEL) ? 50 : 30;
    int limiarDiversao = (personalidade == PERSONALIDADE_SENSIVEL) ? 50 : 30;
    if ((fome <= limiarFome && diversao <= limiarDiversao) || estaDoente) {
      saude = max(0, saude - 1);
      salvarEstado();
    }
    tempoUltimaSaude = millis();
  }

  // --- 3. ALERTA PERIÓDICO DE ATENÇÃO DO BUZZER ---
  int limiarAlerta = (personalidade == PERSONALIDADE_SENSIVEL) ? 50 : 30;
  int limiarUrgente = (personalidade == PERSONALIDADE_SENSIVEL) ? 25 : 15;
  if (fome <= limiarAlerta || diversao <= limiarAlerta || saude <= limiarAlerta) {
    if (millis() - tempoUltimoAlerta >= INTERVALO_ALERTA) {
      if (fome <= limiarUrgente || diversao <= limiarUrgente || saude <= limiarUrgente) {
        // Tom irritado/urgente se estiver bravo (bip-bip rápido e agudo)
        tone(buzzerPin, 523, 100); delay(150);
        tone(buzzerPin, 523, 100);
      } else {
        // Tom melancólico duplo (bip-bip)
        tone(buzzerPin, 330, 150); delay(200);
        tone(buzzerPin, 262, 250);
      }
      tempoUltimoAlerta = millis();
    }
  }

  // --- 4. VERIFICAÇÃO PERIÓDICA DO VÍNCULO (Personalidade Dinâmica) ---
  if (millis() - tempoUltimaVerificacaoVinculo >= 10000) { // Verifica a cada 10 segundos
    tempoUltimaVerificacaoVinculo = millis();
    
    // Se bem cuidado: fome, diversão e saúde altos e sem doença
    if (fome >= 70 && diversao >= 70 && saude >= 70 && !estaDoente) {
      atualizarVinculo(1); // Ganha +1 ponto de vínculo
    }
    
    // Se negligenciado: fome ou diversão baixas (sem comida ou sem brincar)
    int limiarNegligencia = 30;
    if (fome <= limiarNegligencia || diversao <= limiarNegligencia) {
      atualizarVinculo(-3); // Perde -3 pontos de vínculo (regride de personalidade)
    }
  }

  // --- DOENÇA POR CLIMA E ESPIRROS ---
  // Se o robô ficar no frio (temperatura < 25), roda uma probabilidade pequena a cada 1 minuto (60 segundos)
  // de ficar resfriado (média de 1 vez por semana considerando 8 horas de frio por noite = 1 chance em 3360 por minuto)
  if (temperatura < 25 && temperatura > 0 && !estaDoente) {
    if (tempoNoFrio == 0) {
      tempoNoFrio = millis();
    } else if (millis() - tempoNoFrio >= 60000) {
      tempoNoFrio = millis(); // Reinicia o timer de 1 minuto para a próxima verificação
      if (random(0, 3360) == 0) {
        estaDoente = true;
        salvarEstado();
        // Melodia triste de doença:
        tone(buzzerPin, 220, 300); delay(350);
        tone(buzzerPin, 196, 500);
      }
    }
  } else if (temperatura >= 25 || temperatura <= 0) {
    tempoNoFrio = 0;
  }

  // Lógica do Espirro (se estiver doente, espirra de tempos em tempos)
  if (estaDoente) {
    if (millis() - tempoUltimoEspirro >= tempoProximoEspirro) {
      // Ativa animação de espirro
      animacaoEspirro = true;
      tempoInicioEspirro = millis();
      
      // Som de espirro (A-choo!)
      tone(buzzerPin, 880, 80); delay(100);
      tone(buzzerPin, 440, 150);
      
      tempoUltimoEspirro = millis();
      tempoProximoEspirro = random(12000, 25000); // Espirra a cada 12 a 25s
    }
  }

  // --- 4. LEITURA DO LEITOR RFID (Com Auto-Aprendizado) ---
  bool leuCartao = false;
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    leuCartao = true;
    byte* cardUID = rfid.uid.uidByte;
    
    // Imprime o UID do cartão no Serial Monitor
    Serial.print(F("RFID lido:"));
    for (byte i = 0; i < rfid.uid.size; i++) {
      Serial.print(cardUID[i] < 0x10 ? " 0" : " ");
      Serial.print(cardUID[i], HEX);
    }
    Serial.println();
    
    // LÓGICA DE COMPARAÇÃO / APRENDIZADO
    if (!eUIDZerado(uidComida) && comparaUID(cardUID, uidComida)) {
      alimentarPet();
    }
    else if (!eUIDZerado(uidBrincar) && comparaUID(cardUID, uidBrincar)) {
      brincarPet();
    }
    else if (!eUIDZerado(uidSaude) && comparaUID(cardUID, uidSaude)) {
      curarPet();
    }
    else if (!eUIDZerado(uidPet) && comparaUID(cardUID, uidPet)) {
      acariciarPet();
      petTagPresente = true;
    }
    else {
      // Aprende o cartão na ordem
      if (eUIDZerado(uidComida)) {
        copiaUID(uidComida, cardUID);
        Serial.println(F("-> Cartao registrado para COMIDA!"));
        mostrarMensagemRegistro("COMIDA");
        alimentarPet();
      }
      else if (eUIDZerado(uidBrincar)) {
        if (!comparaUID(cardUID, uidComida)) {
          copiaUID(uidBrincar, cardUID);
          Serial.println(F("-> Cartao registrado para BRINCAR!"));
          mostrarMensagemRegistro("BRINCAR");
          brincarPet();
        }
      }
      else if (eUIDZerado(uidSaude)) {
        if (!comparaUID(cardUID, uidComida) && !comparaUID(cardUID, uidBrincar)) {
          copiaUID(uidSaude, cardUID);
          Serial.println(F("-> Cartao registrado para SAUDE!"));
          mostrarMensagemRegistro("SAUDE");
          curarPet();
        }
      }
      else if (eUIDZerado(uidPet)) {
        if (!comparaUID(cardUID, uidComida) && !comparaUID(cardUID, uidBrincar) && !comparaUID(cardUID, uidSaude)) {
          copiaUID(uidPet, cardUID);
          Serial.println(F("-> Cartao registrado para PET!"));
          mostrarMensagemRegistro("GATINHO (PET)");
          acariciarPet();
          petTagPresente = true;
        }
      }
    }
    
    rfid.PICC_HaltA(); // Pausa leitura para evitar repetição
  }

  // Verificar se o cartão do pet ainda está no leitor
  static unsigned long tempoUltimaVerificacaoPet = 0;
  if (petTagPresente && (millis() - tempoUltimaVerificacaoPet >= 500)) {
    tempoUltimaVerificacaoPet = millis();
    
    byte bufferATQA[2];
    byte bufferSize = sizeof(bufferATQA);
    
    // Acorda o cartão caso ele esteja no campo (mesmo em Halt)
    MFRC522::StatusCode status = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
    
    bool aindaPresente = false;
    if (status == MFRC522::STATUS_OK) {
      if (rfid.PICC_ReadCardSerial()) {
        if (comparaUID(rfid.uid.uidByte, uidPet)) {
          aindaPresente = true;
        }
        rfid.PICC_HaltA(); // Deixa em Halt novamente
      }
    }
    
    if (!aindaPresente) {
      petTagPresente = false;
      Serial.println(F("Etiqueta do Pet removida!"));
    }
  }

  // Desativa reações após o tempo estipulado
  if (animacaoSusto && (millis() - tempoInicioSusto >= DURACAO_SUSTO)) {
    animacaoSusto = false;
  }
  if (animacaoEspirro && (millis() - tempoInicioEspirro >= 1500)) {
    animacaoEspirro = false;
  }
  if ((animacaoComendo || animacaoBrincando || animacaoCurando || animacaoAcariciado) && 
      (millis() - tempoInicioReacao >= DURACAO_REACAO)) {
    animacaoComendo = false;
    animacaoBrincando = false;
    animacaoCurando = false;
    animacaoAcariciado = false;
  }

  // --- 4. LEITURA DOS SENSORES ---
  // Captura o sinal de som em uma janela rápida de ~1.5ms (15 amostras) e calcula o desvio pico-a-pico
  // para cancelar o DC offset (potenciômetro descalibrado) e evitar falsos positivos constantes
  int maxLeitura = 0;
  int minLeitura = 1023;
  for (int i = 0; i < 15; i++) {
    int leitura = analogRead(somAnalogPin);
    if (leitura > maxLeitura) maxLeitura = leitura;
    if (leitura < minLeitura) minLeitura = leitura;
  }
  int somMaxDev = maxLeitura - minLeitura; // Sem dividir por 2 para aumentar a sensibilidade

  // Lógica de reação ao som baseada em dois níveis de barulho (silenciosa por som)
  if (somMaxDev >= LIMIAR_SUSTO) {
    if (!animacaoSusto) {
      Serial.println(F("Som ALTO detectado! Susto (Silencioso)!"));
      animacaoSusto = true;
      tempoInicioSusto = millis();
      telaAtual = TELA_FACE;
      
      // Som do buzzer removido a pedido do usuário
    }
  }
  else if (somMaxDev >= LIMIAR_BRINCAR) {
    // Se o pet não estiver fazendo outra animação, ele entende o som intermediário como uma brincadeira
    if (!animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoSusto) {
      Serial.println(F("Som intermediario detectado! Brincando (Silencioso)!"));
      brincarPet(false); // Executa a brincadeira sem tocar melodia no buzzer
    }
  }

  long duracao, distancia;
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duracao = pulseIn(echoPin, HIGH, 3000); // Timeout otimizado de 3ms (~51cm max) para evitar travar a CPU/FPS do loop de animacao
  distancia = duracao * 0.034 / 2;

  if (millis() - tempoUltimaLeituraDHT >= 2000) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperatura = t; umidade = h;
    }
    tempoUltimaLeituraDHT = millis();
  }

  // Depuração temporária do sensor de luz (luzPorcento e ldrValor lidos no início do loop)
  static unsigned long tempoUltimoPrintLDR = 0;
  if (millis() - tempoUltimoPrintLDR >= 1000) {
    Serial.print(F("LDR Raw: ")); Serial.print(ldrValor);
    Serial.print(F(" | Luz %: ")); Serial.println(luzPorcento);
    tempoUltimoPrintLDR = millis();
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);
  ax = Wire.read()<<8|Wire.read();
  ay = Wire.read()<<8|Wire.read();
  az = Wire.read()<<8|Wire.read();

  long variacaoMovimento = abs(ax - ax_ant) + abs(ay - ay_ant) + abs(az - az_ant);
  ax_ant = ax; ay_ant = ay; az_ant = az;

  // --- 3. FÍSICA E CÁLCULO DAS ANIMAÇÕES DOS OLHOS ---
  
  // Avança o ângulo de rotação da tontura
  anguloTontura += 0.25;
  if (anguloTontura > 2 * 3.14159) {
    anguloTontura -= 2 * 3.14159;
  }

  // Posições base dos olhos
  float baseLx = 35.0;
  float baseLy = 32.0;
  float baseRx = 93.0;
  float baseRy = 32.0;
  float baseLw = 20.0;
  float baseLh = 30.0;
  float baseRw = 20.0;
  float baseRh = 30.0;
  float baseRadius = 6.0;

  // Ajustes de Base por Personalidade
  if (personalidade == PERSONALIDADE_SENSIVEL) {
    baseLw = 22.0; // Olhos ligeiramente maiores
    baseRw = 22.0;
    baseLh = 32.0;
    baseRh = 32.0;
    baseRadius = 10.0; // Mais arredondados (olhar brilhante/fofo)
  }


  // Lógica de Prioridades de Expressão Baseada em Sensores
  if (animacaoSusto) {
    // SUSTO / SURPRESO (Detecção de som alto)
    targetLw = 26.0; targetLh = 42.0;
    targetRw = 26.0; targetRh = 42.0;
    targetRadius = 13.0;
    // Tremor de susto
    targetLx = baseLx + random(-2, 3);
    targetLy = baseLy + random(-2, 3);
    targetRx = baseRx + random(-2, 3);
    targetRy = baseRy + random(-2, 3);

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  }
  else if (animacaoBrincando) {
    // ANIMADO / CURIOSO: Olhos bem arredondados, olhando ao redor com curiosidade
    targetRadius = 7.0; // Torna os cantos muito suaves, dando aparência arredondada
    
    // Movimento de "olhar ao redor" com curiosidade (deslocamentos maiores para o lado)
    unsigned long tempoDecorrido = millis() - tempoInicioReacao;
    float lookX = 0;
    float lookY = 0;
    
    // Muda a direção do olhar a cada 600ms (desloca bastante para os lados)
    int faseOlhar = (tempoDecorrido / 600) % 5;
    if (faseOlhar == 0) { lookX = -8; lookY = -2; }      // Olha para a esquerda-cima
    else if (faseOlhar == 1) { lookX = 8; lookY = -2; }  // Olha para a direita-cima
    else if (faseOlhar == 2) { lookX = 0; lookY = 4; }   // Olha para baixo/frente
    else if (faseOlhar == 3) { lookX = -7; lookY = 2; }  // Olha para a esquerda-baixo
    else if (faseOlhar == 4) { lookX = 7; lookY = 2; }   // Olha para a direita-baixo
    
    // Balanço amigável (cabeça gingando suavemente)
    float swayX = sin(millis() * 0.008) * 1.5;
    float swayY = cos(millis() * 0.008) * 1.0;
    
    // LÓGICA DO OLHO MAIOR NA DIREÇÃO DO OLHAR E APROXIMAÇÃO:
    float extraLx = 0;
    float extraRx = 0;

    if (lookX > 0) {
      // Olhando para a direita: 
      // 1. Olho direito maior (24x24), esquerdo menor (16x16)
      targetLw = 16.0; targetLh = 16.0;
      targetRw = 24.0; targetRh = 24.0;
      // 2. Aproxime o olho esquerdo do direito (desloque o esquerdo para a direita)
      extraLx = 8.0; 
    } 
    else if (lookX < 0) {
      // Olhando para a esquerda:
      // 1. Olho esquerdo maior (24x24), direito menor (16x16)
      targetLw = 24.0; targetLh = 24.0;
      targetRw = 16.0; targetRh = 16.0;
      // 2. Aproxime o olho direito do esquerdo (desloque o direito para a esquerda)
      extraRx = -8.0;
    } 
    else {
      // Olhando para o centro: olhos normais e simétricos
      targetLw = 20.0; targetLh = 20.0;
      targetRw = 20.0; targetRh = 20.0;
    }

    // Calcula posições com deslocamentos
    targetLx = baseLx + lookX + swayX + extraLx;
    targetLy = baseLy + lookY + swayY;
    targetRx = baseRx + lookX + swayX + extraRx;
    targetRy = baseRy + lookY + swayY;
    
    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  }
  else if (variacaoMovimento > 8000) {
    // TONTO: Sacudida brusca
    targetLw = 18.0; targetLh = 18.0;
    targetRw = 18.0; targetRh = 18.0;
    targetRadius = 9.0;
    targetLx = baseLx; targetLy = baseLy;
    targetRx = baseRx; targetRy = baseRy;
    
    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (distancia > 0 && distancia < 10) {
    // SUSTO / SURPRESO (Perto demais)
    targetLw = 26.0; targetLh = 42.0;
    targetRw = 26.0; targetRh = 42.0;
    targetRadius = 13.0;
    // Tremor rápido devido ao susto
    targetLx = baseLx + random(-2, 3);
    targetLy = baseLy + random(-2, 3);
    targetRx = baseRx + random(-2, 3);
    targetRy = baseRy + random(-2, 3);

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (distancia >= 10 && distancia < 25) {
    // FOCO / CURIOSO (Distância Média - Olha vesgo para o centro)
    targetLw = 20.0; targetLh = 30.0;
    targetRw = 20.0; targetRh = 30.0;
    targetRadius = 6.0;
    targetLx = baseLx + 4.0; 
    targetLy = baseLy;
    targetRx = baseRx - 4.0; 
    targetRy = baseRy;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (fome <= (personalidade == PERSONALIDADE_SENSIVEL ? 25 : 15) || 
           diversao <= (personalidade == PERSONALIDADE_SENSIVEL ? 25 : 15) || 
           saude <= (personalidade == PERSONALIDADE_SENSIVEL ? 25 : 15)) {
    // BRAVO / MUITO NEGLIGENCIADO (Sem atenção por muito tempo)
    targetLw = 20.0; targetLh = 18.0;
    targetRw = 20.0; targetRh = 18.0;
    targetRadius = 4.0;
    
    // Animação de respiração raivosa rápida + pequeno tremor lateral
    float tremorX = sin(millis() * 0.02) * 0.5;
    float breathY = sin(millis() * 0.008) * 0.8;
    targetLx = baseLx + tremorX;
    targetLy = baseLy + 1.0 + breathY;
    targetRx = baseRx + tremorX;
    targetRy = baseRy + 1.0 + breathY;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  }
  else if (fome <= (personalidade == PERSONALIDADE_SENSIVEL ? 50 : 30) || 
           diversao <= (personalidade == PERSONALIDADE_SENSIVEL ? 50 : 30) || 
           saude <= (personalidade == PERSONALIDADE_SENSIVEL ? 50 : 30) || 
           estaDoente) {
    // TRISTE / FAMINTO / ENTEDIADO / DOENTE
    targetLw = 20.0; targetLh = 16.0;
    targetRw = 20.0; targetRh = 16.0;
    targetRadius = 4.0;
    
    // Animação de respiração triste lenta e olhos ligeiramente caídos
    float breathY = sin(millis() * 0.0025) * 1.0;
    targetLx = baseLx;
    targetLy = baseLy + 3.0 + breathY;
    targetRx = baseRx;
    targetRy = baseRy + 3.0 + breathY;

    suor1.ativa = false; suor2.ativa = false;
    zZzVal.ativa = false;

    // Se for sensível, chora (gotas de lágrima) com facilidade
    if (personalidade == PERSONALIDADE_SENSIVEL) {
      if (!lagrimaL.ativa && random(0, 100) < 12) {
        lagrimaL.ativa = true;
        lagrimaL.x = baseLx - 8 + random(0, 4);
        lagrimaL.y = baseLy + 10;
        lagrimaL.velocidade = 0.7 + (random(0, 4) / 10.0);
      }
      if (!lagrimaR.ativa && random(0, 100) < 12) {
        lagrimaR.ativa = true;
        lagrimaR.x = baseRx + 4 + random(0, 4);
        lagrimaR.y = baseLy + 10;
        lagrimaR.velocidade = 0.7 + (random(0, 4) / 10.0);
      }
    } else {
      lagrimaL.ativa = false; lagrimaR.ativa = false;
    }
    
    // Processamento de queda física das lágrimas
    if (lagrimaL.ativa) {
      lagrimaL.y += lagrimaL.velocidade;
      if (lagrimaL.y > 64) lagrimaL.ativa = false;
    }
    if (lagrimaR.ativa) {
      lagrimaR.y += lagrimaR.velocidade;
      if (lagrimaR.y > 64) lagrimaR.ativa = false;
    }
  }
  else if (luzPorcento < 10) {
    // SONO: Luz apagada
    targetLw = 24.0; targetLh = 3.0;
    targetRw = 24.0; targetRh = 3.0;
    targetRadius = 1.0;
    
    // Respiração lenta (sobe e desce)
    float breath = sin(millis() * 0.002) * 1.8;
    targetLx = baseLx; targetLy = baseLy + breath;
    targetRx = baseRx; targetRy = baseRy + breath;

    // Lógica da letra zZz subindo
    if (millis() - tempoUltimozZz > 1800) {
      zZzVal.ativa = true;
      zZzVal.x = 76.0;
      zZzVal.y = 42.0;
      zZzVal.tamanho = 1.0;
      zZzVal.fase = 0.0;
      tempoUltimozZz = millis();
    }
    if (zZzVal.ativa) {
      zZzVal.y -= 0.5;
      zZzVal.fase += 0.08;
      zZzVal.x = 76.0 + sin(zZzVal.fase) * 4.0;
      zZzVal.tamanho += 0.015;
      if (zZzVal.y < 5) zZzVal.ativa = false;
    }

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
  } 
  else if (luzPorcento > 80) {
    // CLARO / ESPREMIDO: Luz muito forte
    targetLw = 24.0; targetLh = 8.0;
    targetRw = 24.0; targetRh = 8.0;
    targetRadius = 2.0;
    targetLx = baseLx; targetLy = baseLy + 3.0; // Espreme os olhos e olha levemente para baixo
    targetRx = baseRx; targetRy = baseRy + 3.0;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (temperatura > 29) {
    // QUENTE: Temperatura alta
    targetLw = 20.0; targetLh = 22.0; 
    targetRw = 20.0; targetRh = 22.0;
    targetRadius = 6.0;
    targetLx = baseLx; targetLy = baseLy + 4.0; // Olhos cansados e baixos
    targetRx = baseRx; targetRy = baseRy + 4.0;

    // Gotas de suor escorrendo pelas laterais da tela
    if (!suor1.ativa && random(0, 100) < 5) {
      suor1.ativa = true;
      suor1.x = 10 + random(0, 15);
      suor1.y = 0;
      suor1.velocidade = 0.8 + (random(0, 5) / 10.0);
    }
    if (suor1.ativa) {
      suor1.y += suor1.velocidade;
      if (suor1.y > 64) suor1.ativa = false;
    }

    if (!suor2.ativa && random(0, 100) < 5) {
      suor2.ativa = true;
      suor2.x = 105 + random(0, 15);
      suor2.y = 0;
      suor2.velocidade = 0.8 + (random(0, 5) / 10.0);
    }
    if (suor2.ativa) {
      suor2.y += suor2.velocidade;
      if (suor2.y > 64) suor2.ativa = false;
    }

    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (temperatura < 25 && temperatura > 0) {
    // FRIO: Temperatura baixa
    targetLw = 12.0; targetLh = 12.0;
    targetRw = 12.0; targetRh = 12.0;
    targetRadius = 4.0;
    
    // Tremor de frio
    targetLx = baseLx + random(-2, 3);
    targetLy = baseLy + random(-2, 3);
    targetRx = baseRx + random(-2, 3);
    targetRy = baseRy + random(-2, 3);

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (umidade > 75) {
    // ÚMIDO: Olhos chorosos / molhados
    targetLw = 22.0; targetLh = 32.0;
    targetRw = 22.0; targetRh = 32.0;
    targetRadius = 9.0;
    targetLx = baseLx; targetLy = baseLy - 2.0; 

    // Lágrimas escorrendo dos cantos dos olhos
    if (!lagrimaL.ativa && random(0, 100) < 8) {
      lagrimaL.ativa = true;
      lagrimaL.x = baseLx - 8 + random(0, 4);
      lagrimaL.y = baseLy + 10;
      lagrimaL.velocidade = 0.7 + (random(0, 4) / 10.0);
    }
    if (lagrimaL.ativa) {
      lagrimaL.y += lagrimaL.velocidade;
      if (lagrimaL.y > 64) lagrimaL.ativa = false;
    }

    if (!lagrimaR.ativa && random(0, 100) < 8) {
      lagrimaR.ativa = true;
      lagrimaR.x = baseRx + 4 + random(0, 4);
      lagrimaR.y = baseLy + 10;
      lagrimaR.velocidade = 0.7 + (random(0, 4) / 10.0);
    }
    if (lagrimaR.ativa) {
      lagrimaR.y += lagrimaR.velocidade;
      if (lagrimaR.y > 64) lagrimaR.ativa = false;
    }

    suor1.ativa = false; suor2.ativa = false;
    zZzVal.ativa = false;
  } 
  else {
    // NORMAL / ATIVO: Condições normais (Ocioso)
    targetLw = baseLw; targetLh = baseLh;
    targetRw = baseRw; targetRh = baseRh;
    targetRadius = baseRadius;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;

    // Movimento Ocioso de Olhar ao Redor
    if (!estaPiscando) {
      if (millis() - tempoUltimoOlharOcioso > tempoProximoOlharOcioso) {
        float lookX = 0;
        float lookY = 0;
        if (personalidade == PERSONALIDADE_SARCASTICA) {
          // Sequência de revirar os olhos (ciclo de olhar em volta)
          static byte faseRolada = 0;
          faseRolada = (faseRolada + 1) % 5;
          if (faseRolada == 0) { lookX = 0; lookY = 0; tempoProximoOlharOcioso = random(3000, 5000); }       // Centro
          else if (faseRolada == 1) { lookX = -7; lookY = 2; tempoProximoOlharOcioso = 800; }                  // Esquerda-baixo
          else if (faseRolada == 2) { lookX = -6; lookY = -4; tempoProximoOlharOcioso = 600; }                 // Esquerda-cima
          else if (faseRolada == 3) { lookX = 6; lookY = -4; tempoProximoOlharOcioso = 600; }                  // Direita-cima
          else if (faseRolada == 4) { lookX = 7; lookY = 2; tempoProximoOlharOcioso = 800; }                   // Direita-baixo
        } else {
          lookX = random(-5, 6);
          lookY = random(-4, 5);
          tempoProximoOlharOcioso = random(2500, 6000);
        }
        targetLx = baseLx + lookX;
        targetLy = baseLy + lookY;
        targetRx = baseRx + lookX;
        targetRy = baseRy + lookY;
        
        tempoUltimoOlharOcioso = millis();
      }
    }
  }

  // --- LÓGICA DE PISCADA INDEPENDENTE DA EXPRESSÃO ---
  // A piscada só acontece se o robô estiver acordado (luzPorcento >= 10)
  if (luzPorcento >= 10) {
    if (!estaPiscando) {
      if (millis() - tempoUltimaPiscada > tempoProximaPiscada) {
        estaPiscando = true;
        duracaoPiscada = millis();
        tempoUltimaPiscada = millis();
        tempoProximaPiscada = random(3000, 6001); // Intervalo aleatório entre 3 e 6 segundos (3000 a 6000 ms)
      }
    } else {
      if (millis() - duracaoPiscada > 180) { // Duração aumentada para 180ms para dar tempo do Lerp fechar o olho
        estaPiscando = false;
      } else {
        targetLh = 1.0;
        targetRh = 1.0;
        targetRadius = 1.0;
      }
    }
  } else {
    estaPiscando = false; // Garante que não pisca enquanto estiver dormindo
  }

  // Interpolação suave em direção aos valores alvo (Lerp)
  float speedH = estaPiscando ? 0.65 : INTERP_SPEED;
  float speedRadius = estaPiscando ? 0.65 : INTERP_SPEED;

  eyeLx += (targetLx - eyeLx) * INTERP_SPEED;
  eyeLy += (targetLy - eyeLy) * INTERP_SPEED;
  eyeRx += (targetRx - eyeRx) * INTERP_SPEED;
  eyeRy += (targetRy - eyeRy) * INTERP_SPEED;
  eyeLw += (targetLw - eyeLw) * INTERP_SPEED;
  eyeLh += (targetLh - eyeLh) * speedH;
  eyeRw += (targetRw - eyeRw) * INTERP_SPEED;
  eyeRh += (targetRh - eyeRh) * speedH;
  eyeRadius += (targetRadius - eyeRadius) * speedRadius;

  // Atualização física do gatinho mini andando (fora do loop de renderização para rodar apenas uma vez por frame)
  if (petTagPresente && !animacaoAcariciado && luzPorcento >= 10) {
    if (millis() - tempoUltimoPasso >= 150) {
      tempoUltimoPasso = millis();
      if (catMiniDirRight) {
        catMiniX += 1.0;
        if (catMiniX >= 118.0) catMiniDirRight = false;
      } else {
        catMiniX -= 1.0;
        if (catMiniX <= 96.0) catMiniDirRight = true;
      }
    }
  }

  // --- 4. RENDERIZAÇÃO DA TELA ---
  u8g2.firstPage();
  do {
    if (telaAtual == TELA_TEMP) {
      // ===== TELA DE STATUS: TEMPERATURA =====
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(0, 12, "TEMPERATURA");
      u8g2.drawLine(0, 16, 75, 16);
      
      // Formata e exibe o valor numérico usando 7 segmentos procedurais
      int tempVal = (int)temperatura;
      draw7SegmentNumber(0, 22, tempVal);
      int w = get7SegmentNumberWidth(tempVal);
      
      // Desenha o símbolo °C
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawCircle(w + 3, 24, 1);
      u8g2.drawStr(w + 7, 32, "C");
      
      // Define a descrição do status e o ícone correspondente
      if (temperatura > 29) {
        u8g2.drawStr(0, 58, "CALOR");
        // Ícone de Sol
        u8g2.drawCircle(100, 32, 10);
        u8g2.drawLine(100, 16, 100, 20);
        u8g2.drawLine(100, 44, 100, 48);
        u8g2.drawLine(84, 32, 88, 32);
        u8g2.drawLine(112, 32, 116, 32);
        u8g2.drawLine(89, 21, 92, 24);
        u8g2.drawLine(111, 21, 108, 24);
        u8g2.drawLine(89, 43, 92, 40);
        u8g2.drawLine(111, 43, 108, 40);
      } 
      else if (temperatura < 25) {
        u8g2.drawStr(0, 58, "FRIO");
        // Ícone de Floco de Neve
        u8g2.drawLine(100, 16, 100, 48);
        u8g2.drawLine(84, 32, 116, 32);
        u8g2.drawLine(89, 21, 111, 43);
        u8g2.drawLine(89, 43, 111, 21);
        u8g2.drawLine(96, 22, 100, 18); u8g2.drawLine(104, 22, 100, 18);
        u8g2.drawLine(96, 42, 100, 46); u8g2.drawLine(104, 42, 100, 46);
        u8g2.drawLine(90, 28, 86, 32); u8g2.drawLine(90, 36, 86, 32);
        u8g2.drawLine(110, 28, 114, 32); u8g2.drawLine(110, 36, 114, 32);
      } 
      else {
        u8g2.drawStr(0, 58, "AGRADAVEL");
        // Ícone de Termômetro
        u8g2.drawCircle(100, 44, 6);
        u8g2.drawFrame(98, 16, 5, 24);
        u8g2.drawBox(99, 26, 3, 16);
        u8g2.drawDisc(100, 44, 4);
        u8g2.setDrawColor(0); u8g2.drawLine(99, 40, 101, 40); u8g2.setDrawColor(1);
      }
    } 
    else if (telaAtual == TELA_UMID) {
      // ===== TELA DE STATUS: UMIDADE =====
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(0, 12, "UMIDADE");
      u8g2.drawLine(0, 16, 75, 16);
      
      // Formata e exibe o valor numérico usando 7 segmentos procedurais
      int umidVal = (int)umidade;
      draw7SegmentNumber(0, 22, umidVal);
      int w = get7SegmentNumberWidth(umidVal);
      
      // Símbolo %
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(w + 3, 36, "%");
      
      // Define a descrição do status e o ícone correspondente
      if (umidade > 75) {
        u8g2.drawStr(0, 58, "ALTA UMID");
        // Ícone de Gota de Água
        u8g2.drawDisc(100, 36, 10);
        u8g2.drawTriangle(100, 16, 91, 32, 109, 32);
        u8g2.setDrawColor(0); u8g2.drawDisc(96, 32, 2); u8g2.setDrawColor(1);
      } 
      else if (umidade < 35) {
        u8g2.drawStr(0, 58, "AR SECO");
        // Ícone de Cacto
        u8g2.drawRBox(97, 18, 6, 30, 2); 
        u8g2.drawBox(91, 28, 7, 4); 
        u8g2.drawRBox(91, 20, 4, 10, 1); 
        u8g2.drawBox(102, 32, 7, 4); 
        u8g2.drawRBox(105, 22, 4, 12, 1); 
        u8g2.drawBox(92, 48, 16, 6); 
      } 
      else {
        u8g2.drawStr(0, 58, "AGRADAVEL");
        // Ícone de Nuvem
        u8g2.drawDisc(92, 38, 8);
        u8g2.drawDisc(100, 32, 10);
        u8g2.drawDisc(109, 38, 7);
        u8g2.drawBox(84, 38, 32, 8);
      }
    } 
    else if (telaAtual == TELA_PET_STATUS) {
      // ===== TELA DE STATUS: TAMAGOTCHI DASHBOARD =====
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(0, 11, "STATUS DO PET");
      u8g2.drawLine(0, 13, 128, 13);
      
      // FOME
      u8g2.drawStr(0, 26, "FOME:");
      u8g2.drawFrame(45, 20, 50, 7);
      u8g2.drawBox(47, 22, fome / 2, 3);
      char fomeStr[8]; itoa(fome, fomeStr, 10);
      u8g2.drawStr(100, 26, fomeStr);
      u8g2.drawStr(120, 26, "%");
      
      // DIVERSÃO (PLAY)
      u8g2.drawStr(0, 42, "BRINCAR:");
      u8g2.drawFrame(45, 36, 50, 7);
      u8g2.drawBox(47, 38, diversao / 2, 3);
      char divStr[8]; itoa(diversao, divStr, 10);
      u8g2.drawStr(100, 42, divStr);
      u8g2.drawStr(120, 42, "%");
      
      // SAÚDE
      u8g2.drawStr(0, 58, "SAUDE:");
      u8g2.drawFrame(45, 50, 50, 7);
      u8g2.drawBox(47, 52, saude / 2, 3);
      char sauStr[8]; itoa(saude, sauStr, 10);
      u8g2.drawStr(100, 58, sauStr);
      u8g2.drawStr(120, 58, "%");
    }
    else {
      // ===== TELA DE EXPRESSÕES DINÂMICAS (Rosto do Robô) =====
      if (animacaoSusto) {
        // Olhos arregalados tremendo com pupilas pretas (Susto por Som)
        drawEye(eyeLx, eyeLy, eyeLw, eyeLh, eyeRadius);
        drawEye(eyeRx, eyeRy, eyeRw, eyeRh, eyeRadius);
        
        u8g2.setDrawColor(0);
        u8g2.drawFilledEllipse((int)eyeLx, (int)eyeLy, 4, 4);
        u8g2.drawFilledEllipse((int)eyeRx, (int)eyeRy, 4, 4);
        u8g2.setDrawColor(1);

        // Boca de susto (círculo aberto)
        u8g2.drawCircle(64, 50, 3);
      }
      else if (animacaoEspirro) {
        // Espirro: Olhos "> <" espremidos (mais expressivos que X X)
        u8g2.drawLine((int)eyeLx - 8, (int)eyeLy - 6, (int)eyeLx + 4, (int)eyeLy);
        u8g2.drawLine((int)eyeLx - 8, (int)eyeLy + 6, (int)eyeLx + 4, (int)eyeLy);
        
        u8g2.drawLine((int)eyeRx + 8, (int)eyeRy - 6, (int)eyeRx - 4, (int)eyeRy);
        u8g2.drawLine((int)eyeRx + 8, (int)eyeRy + 6, (int)eyeRx - 4, (int)eyeRy);
        
        // Boca aberta de espirro
        u8g2.drawDisc(64, 50, 5);
        
        // Gotinhas de espirro voando para fora da boca (X=64, Y=50)
        int tempoSneeze = millis() - tempoInicioEspirro;
        int drop1X = 60 - (tempoSneeze / 20);
        int drop1Y = 52 + (tempoSneeze / 25);
        int drop2X = 64;
        int drop2Y = 54 + (tempoSneeze / 15);
        int drop3X = 68 + (tempoSneeze / 20);
        int drop3Y = 52 + (tempoSneeze / 25);

        if (drop1Y < 64 && drop1X >= 0) u8g2.drawVLine(drop1X, drop1Y, 2);
        if (drop2Y < 64) u8g2.drawVLine(drop2X, drop2Y, 2);
        if (drop3Y < 64 && drop3X < 128) u8g2.drawVLine(drop3X, drop3Y, 2);
      }
      else if (animacaoComendo) {
        // Mastigando: Olhos felizes ^ ^
        u8g2.drawLine((int)eyeLx - 10, (int)eyeLy + 2, (int)eyeLx, (int)eyeLy - 8);
        u8g2.drawLine((int)eyeLx, (int)eyeLy - 8, (int)eyeLx + 10, (int)eyeLy + 2);
        
        u8g2.drawLine((int)eyeRx - 10, (int)eyeRy + 2, (int)eyeRx, (int)eyeRy - 8);
        u8g2.drawLine((int)eyeRx, (int)eyeRy - 8, (int)eyeRx + 10, (int)eyeRy + 2);
        
        // Boca mastigando
        int mastigando = (millis() / 200) % 2;
        if (mastigando == 0) {
          u8g2.drawDisc(64, 48, 5); // Aberta
        } else {
          u8g2.drawLine(59, 48, 69, 48); // Fechada
        }
        
        // Partículas de comida subindo
        int subida = (millis() - tempoInicioReacao) / 40;
        int offsetStr = (int)(sin(millis() * 0.01) * 4.0);
        u8g2.setFont(u8g2_font_profont11_tr);
        u8g2.drawStr(64 + offsetStr, 40 - (subida % 25), "*");
        u8g2.drawStr(60 - offsetStr, 35 - ((subida + 10) % 25), "*");
      }
      else if (animacaoBrincando) {
        // Brincando: Olhos arredondados móveis e dinâmicos (tamanhos assimétricos dependendo da direção)
        drawEye(eyeLx, eyeLy, eyeLw, eyeLh, eyeRadius);
        drawEye(eyeRx, eyeRy, eyeRw, eyeRh, eyeRadius);
        
        // Sobrancelhas removidas para garantir visual limpo e evitar aparência de bravo

        // Calcula o deslocamento da boca com base na média da posição dos olhos (efeito de perspectiva 3D)
        float avgEyeX = (eyeLx + eyeRx) / 2.0;
        float avgEyeY = (eyeLy + eyeRy) / 2.0;
        float mouthShiftX = (avgEyeX - 64.0) * 0.85;
        float mouthShiftY = (avgEyeY - 32.0) * 0.80;

        // Boca: Formato de gatinho (ω)
        u8g2.drawLine((int)(58 + mouthShiftX), (int)(47 + mouthShiftY), (int)(61 + mouthShiftX), (int)(50 + mouthShiftY));
        u8g2.drawLine((int)(61 + mouthShiftX), (int)(50 + mouthShiftY), (int)(64 + mouthShiftX), (int)(47 + mouthShiftY));
        u8g2.drawLine((int)(64 + mouthShiftX), (int)(47 + mouthShiftY), (int)(67 + mouthShiftX), (int)(50 + mouthShiftY));
        u8g2.drawLine((int)(67 + mouthShiftX), (int)(50 + mouthShiftY), (int)(70 + mouthShiftX), (int)(47 + mouthShiftY));
        
        // Pequenos corações de empolgação flutuando
        int subidaBrincar = (millis() - tempoInicioReacao) / 40;
        int offsetStrBrincar = (int)(cos(millis() * 0.01) * 6.0);
        drawHeart(64 + offsetStrBrincar, 38 - (subidaBrincar % 25));
        drawHeart(64 - offsetStrBrincar, 32 - ((subidaBrincar + 10) % 25));
      }
      else if (animacaoCurando) {
        // Curando: Pisca um olho (Wink)
        // Olho esquerdo normal, olho direito fechado piscando
        drawEye(eyeLx, eyeLy, eyeLw, eyeLh, eyeRadius);
        u8g2.drawLine((int)eyeRx - 10, (int)eyeRy, (int)eyeRx + 10, (int)eyeRy); // olho piscando
        
        // Boca feliz normal
        u8g2.drawLine(59, 48, 64, 51);
        u8g2.drawLine(64, 51, 69, 48);
        
        // Pequenos símbolos de mais (+) subindo para representar cura
        int subida = (millis() - tempoInicioReacao) / 40;
        int offsetStr = (int)(sin(millis() * 0.01) * 4.0);
        u8g2.setFont(u8g2_font_profont11_tr);
        u8g2.drawStr(64 + offsetStr, 38 - (subida % 25), "+");
        u8g2.drawStr(64 - offsetStr, 32 - ((subida + 12) % 25), "+");
      }
      else if (animacaoAcariciado) {
        // Gatinho sendo acariciado (olhos fechados felizes, orelhas, mão acariciando)
        // Cabeça do gatinho no centro
        u8g2.drawRFrame(44, 30, 40, 26, 8); // cabeça
        
        // Orelhas
        u8g2.drawTriangle(44, 30, 52, 30, 40, 18); // orelha esquerda
        u8g2.drawTriangle(84, 30, 76, 30, 88, 18); // orelha direita
        
        // Olhos fechados de felicidade ( ^ ^ )
        u8g2.drawLine(50, 42, 54, 39);
        u8g2.drawLine(54, 39, 58, 42);
        
        u8g2.drawLine(70, 42, 74, 39);
        u8g2.drawLine(74, 39, 78, 42);
        
        // Nariz e boca (w)
        u8g2.drawPixel(64, 44);
        u8g2.drawLine(61, 46, 64, 48);
        u8g2.drawLine(64, 48, 67, 46);
        
        // Bigodes
        u8g2.drawLine(36, 43, 42, 44);
        u8g2.drawLine(35, 47, 42, 46);
        u8g2.drawLine(86, 44, 92, 43);
        u8g2.drawLine(86, 46, 93, 47);
        
        // Mão fazendo carinho (um arco se movendo para a esquerda e direita sobre a cabeça)
        int maoOffset = (int)(sin(millis() * 0.006) * 12.0);
        int maoX = 64 + maoOffset;
        int maoY = 16;
        u8g2.drawCircle(maoX, maoY, 6, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT); // semicírculo representando a mão
        u8g2.drawLine(maoX - 6, maoY, maoX - 10, maoY - 2); // punho/braço
        
        // Corações flutuando
        int subidaCarinho = (millis() - tempoInicioReacao) / 40;
        int offsetStrCarinho = (int)(sin(millis() * 0.015) * 8.0);
        drawHeart(64 + offsetStrCarinho, 25 - (subidaCarinho % 20));
        drawHeart(40 - offsetStrCarinho, 30 - ((subidaCarinho + 8) % 20));
        drawHeart(88 + offsetStrCarinho, 35 - ((subidaCarinho + 14) % 20));
      }
      else if (variacaoMovimento > 8000) {
        // TONTO: Desenha dois "X" giratórios
        float dx1 = 8.0 * cos(anguloTontura);
        float dy1 = 8.0 * sin(anguloTontura);
        float dx2 = -8.0 * sin(anguloTontura);
        float dy2 = 8.0 * cos(anguloTontura);
        
        u8g2.drawLine((int)eyeLx - dx1, (int)eyeLy - dy1, (int)eyeLx + dx1, (int)eyeLy + dy1);
        u8g2.drawLine((int)eyeLx - dx2, (int)eyeLy - dy2, (int)eyeLx + dx2, (int)eyeLy + dy2);
        
        u8g2.drawLine((int)eyeRx - dx1, (int)eyeRy - dy1, (int)eyeRx + dx1, (int)eyeRy + dy1);
        u8g2.drawLine((int)eyeRx - dx2, (int)eyeRy - dy2, (int)eyeRx + dx2, (int)eyeRy + dy2);
        
        // Estrelinhas girando no topo
        float estrelaAngulo = millis() * 0.005;
        int ex1 = 64 + cos(estrelaAngulo) * 12;
        int ey1 = 10 + sin(estrelaAngulo) * 3;
        int ex2 = 64 + cos(estrelaAngulo + 3.1415) * 12;
        int ey2 = 10 + sin(estrelaAngulo + 3.1415) * 3;
        u8g2.setFont(u8g2_font_profont11_tr);
        u8g2.drawStr(ex1 - 2, ey1 + 3, "*");
        u8g2.drawStr(ex2 - 2, ey2 + 3, "*");
      } 
      else {
        // Olhos Rounded Box dinâmicos
        drawEye(eyeLx, eyeLy, eyeLw, eyeLh, eyeRadius);
        drawEye(eyeRx, eyeRy, eyeRw, eyeRh, eyeRadius);
        
        // Se for a personalidade Sarcástica e estiver acordado, desenha pálpebras caídas (oculta no auge da piscada para fechar limpo)
        if (personalidade == PERSONALIDADE_SARCASTICA && luzPorcento >= 10 && !estaPiscando) {
          u8g2.setDrawColor(0); // Desenha em preto para cortar a metade superior
          u8g2.drawBox((int)(eyeLx - eyeLw/2.0 - 2), (int)(eyeLy - eyeLh/2.0 - 1), (int)(eyeLw + 4), (int)(eyeLh * 0.45));
          u8g2.drawBox((int)(eyeRx - eyeRw/2.0 - 2), (int)(eyeRy - eyeRh/2.0 - 1), (int)(eyeRw + 4), (int)(eyeRh * 0.45));
          u8g2.setDrawColor(1); // Restaura cor branca
          
          // Desenha a dobra/vinco da pálpebra logo no corte
          u8g2.drawLine((int)(eyeLx - eyeLw/2.0 - 1), (int)(eyeLy - eyeLh/2.0 + eyeLh * 0.45), (int)(eyeLx + eyeLw/2.0 + 1), (int)(eyeLy - eyeLh/2.0 + eyeLh * 0.45));
          u8g2.drawLine((int)(eyeRx - eyeRw/2.0 - 1), (int)(eyeRy - eyeRh/2.0 + eyeRh * 0.45), (int)(eyeRx + eyeRw/2.0 + 1), (int)(eyeRy - eyeRh/2.0 + eyeRh * 0.45));

          // Novas Sobrancelhas Sarcásticas Padrão:
          // Esquerda: Arqueada suavemente (⌒)
          u8g2.drawLine((int)eyeLx - 9, (int)eyeLy - 10, (int)eyeLx, (int)eyeLy - 14);
          u8g2.drawLine((int)eyeLx, (int)eyeLy - 14, (int)eyeLx + 8, (int)eyeLy - 10);
          
          // Direita: Reta com ponta erguida no canto externo (───╯)
          u8g2.drawLine((int)eyeRx - 8, (int)eyeRy - 11, (int)eyeRx + 3, (int)eyeRy - 11);
          u8g2.drawLine((int)eyeRx + 3, (int)eyeRy - 11, (int)eyeRx + 9, (int)eyeRy - 15);
        }
        
        // Expressões de Bravo e Triste baseadas nos atributos e na personalidade
        int limiarBravo = (personalidade == PERSONALIDADE_SENSIVEL) ? 25 : 15;
        int limiarTriste = (personalidade == PERSONALIDADE_SENSIVEL) ? 50 : 30;
        
        if (fome <= limiarBravo || diversao <= limiarBravo || saude <= limiarBravo) {
          // --- ROSTO BRAVO ---
          int offsetMouthY = (int)(sin(millis() * 0.008) * 0.8); // leve tremor na boca
          // Sobrancelhas bravas (inclinadas para baixo no centro / \ )
          u8g2.drawLine((int)eyeLx - 10, (int)eyeLy - 12, (int)eyeLx + 8, (int)eyeLy - 8);
          u8g2.drawLine((int)eyeRx - 8, (int)eyeRy - 8, (int)eyeRx + 10, (int)eyeRy - 12);
          
          // Boca brava (linha reta que oscila levemente)
          u8g2.drawLine(58, 49 + offsetMouthY, 70, 49 + offsetMouthY);
        }
        else if (fome <= limiarTriste || diversao <= limiarTriste || saude <= limiarTriste || estaDoente) {
          // --- ROSTO TRISTE MELHORADO ---
          int offsetMouthY = (int)(sin(millis() * 0.0025) * 0.6); // respiração lenta da boca
          // Sobrancelhas tristes (inclinadas para cima no centro \ / )
          u8g2.drawLine((int)eyeLx - 8, (int)eyeLy - 10, (int)eyeLx + 8, (int)eyeLy - 14);
          u8g2.drawLine((int)eyeRx - 8, (int)eyeRy - 14, (int)eyeRx + 8, (int)eyeRy - 10);
          
          // Boca triste melhorada (curva para baixo que acompanha a respiração)
          u8g2.drawLine(58, 51 + offsetMouthY, 61, 48 + offsetMouthY);
          u8g2.drawLine(61, 48 + offsetMouthY, 67, 48 + offsetMouthY);
          u8g2.drawLine(67, 48 + offsetMouthY, 70, 51 + offsetMouthY);
        }
        else {
          // --- ROSTO NORMAL / FELIZ ---
          int offsetMouthY = (int)(sin(millis() * 0.0025) * 0.5); // respiração super sutil
          
          if (personalidade == PERSONALIDADE_SARCASTICA) {
            // Boca curvada para baixo (︵) padrão da personalidade sarcástica
            u8g2.drawLine(58, 51 + offsetMouthY, 64, 48 + offsetMouthY);
            u8g2.drawLine(64, 48 + offsetMouthY, 70, 51 + offsetMouthY);
          } else {
            // Boca fofa de gatinho (w) centralizada em X=64
            u8g2.drawLine(60, 48 + offsetMouthY, 62, 50 + offsetMouthY);
            u8g2.drawLine(62, 50 + offsetMouthY, 64, 48 + offsetMouthY);
            u8g2.drawLine(64, 48 + offsetMouthY, 66, 50 + offsetMouthY);
            u8g2.drawLine(66, 50 + offsetMouthY, 68, 48 + offsetMouthY);
          }
        }
        
        // --- BALÃO DE PENSAMENTO (HUD de necessidades do Pet) ---
        int numIcons = 0;
        int limiarHUD = (personalidade == PERSONALIDADE_SENSIVEL) ? 50 : 30;
        bool precisaComida = (fome <= limiarHUD);
        bool precisaBrincar = (diversao <= limiarHUD);
        bool precisaSaude = (saude <= limiarHUD || estaDoente);
        if (precisaComida) numIcons++;
        if (precisaBrincar) numIcons++;
        if (precisaSaude) numIcons++;

        if (numIcons > 0) {
          int totalW = (numIcons * 12) + ((numIcons - 1) * 4);
          int startX = 64 - (totalW / 2);
          int drawY = 4; // Posição Y do balão (topo da tela)
          
          // Moldura do balão de pensamento
          u8g2.drawRFrame(startX - 3, drawY - 2, totalW + 6, 12, 2);
          
          // Pontinhos de pensamento descendo em direção ao rosto
          u8g2.drawPixel(64, drawY + 12);
          u8g2.drawPixel(62, drawY + 14);

          int currentX = startX;
          if (precisaComida) {
            // Desenha um osso para representar fome
            u8g2.drawBox(currentX + 3, drawY + 3, 6, 2);
            u8g2.drawDisc(currentX + 2, drawY + 2, 1);
            u8g2.drawDisc(currentX + 2, drawY + 5, 1);
            u8g2.drawDisc(currentX + 9, drawY + 2, 1);
            u8g2.drawDisc(currentX + 9, drawY + 5, 1);
            currentX += 16;
          }
          if (precisaBrincar) {
            // Desenha um controle de videogame (gamepad) para representar brincadeira
            u8g2.drawRFrame(currentX, drawY - 1, 12, 7, 1);
            u8g2.drawBox(currentX + 2, drawY + 2, 3, 1);
            u8g2.drawBox(currentX + 3, drawY + 1, 1, 3);
            u8g2.drawPixel(currentX + 9, drawY + 1);
            u8g2.drawPixel(currentX + 8, drawY + 3);
            u8g2.drawPixel(currentX + 5, drawY + 2);
            u8g2.drawPixel(currentX + 6, drawY + 2);
            currentX += 16;
          }
          if (precisaSaude) {
            // Desenha um frasco de remédio com cruz para representar saúde
            u8g2.drawFrame(currentX + 2, drawY, 8, 7);
            u8g2.drawBox(currentX + 4, drawY - 2, 4, 2);
            u8g2.drawVLine(currentX + 5, drawY + 2, 3);
            u8g2.drawHLine(currentX + 4, drawY + 3, 3);
            currentX += 16;
          }
        }
        
        // Susto: Desenha pupilas contraídas (pretas)
        if (distancia > 0 && distancia < 10) {
          u8g2.setDrawColor(0);
          u8g2.drawFilledEllipse((int)eyeLx, (int)eyeLy, 4, 4);
          u8g2.drawFilledEllipse((int)eyeRx, (int)eyeRy, 4, 4);
          u8g2.setDrawColor(1);
        }
        
        // Sono: Letras "zZz" flutuando
        if (luzPorcento < 10 && zZzVal.ativa) {
          u8g2.setFont(u8g2_font_profont11_tr);
          if (zZzVal.tamanho < 1.3) {
            u8g2.drawStr((int)zZzVal.x, (int)zZzVal.y, "z");
          } else if (zZzVal.tamanho < 1.7) {
            u8g2.drawStr((int)zZzVal.x, (int)zZzVal.y, "Z");
          } else {
            u8g2.drawStr((int)zZzVal.x, (int)zZzVal.y, "Z");
            u8g2.drawStr((int)zZzVal.x - 4, (int)zZzVal.y + 6, "z");
          }
        }
        
        // Calor: Gotas de suor nas laterais
        if (temperatura > 29) {
          if (suor1.ativa) {
            u8g2.drawBox((int)suor1.x, (int)suor1.y, 2, 4);
          }
          if (suor2.ativa) {
            u8g2.drawBox((int)suor2.x, (int)suor2.y, 2, 4);
          }
          u8g2.drawBox((int)eyeLx - 14, (int)eyeLy - 4, 2, 3);
        }
        
        // Frio: Desenha boca tremendo
        if (temperatura < 25 && temperatura > 0) {
          int tremorMouth = random(-1, 2);
          u8g2.drawLine(54, 48 + tremorMouth, 58, 46 + tremorMouth);
          u8g2.drawLine(58, 46 + tremorMouth, 62, 50 + tremorMouth);
          u8g2.drawLine(62, 50 + tremorMouth, 66, 46 + tremorMouth);
          u8g2.drawLine(66, 46 + tremorMouth, 70, 50 + tremorMouth);
          u8g2.drawLine(70, 50 + tremorMouth, 74, 48 + tremorMouth);
        }
        
        // Úmido ou Personalidade Sensível Triste: Desenha lágrimas e boca triste
        if (umidade > 75 || (personalidade == PERSONALIDADE_SENSIVEL && (lagrimaL.ativa || lagrimaR.ativa))) {
          if (lagrimaL.ativa) {
            u8g2.drawFilledEllipse((int)lagrimaL.x, (int)lagrimaL.y, 2, 3);
            u8g2.drawLine((int)lagrimaL.x, (int)lagrimaL.y - 2, (int)lagrimaL.x, (int)lagrimaL.y);
          }
          if (lagrimaR.ativa) {
            u8g2.drawFilledEllipse((int)lagrimaR.x, (int)lagrimaR.y, 2, 3);
            u8g2.drawLine((int)lagrimaR.x, (int)lagrimaR.y - 2, (int)lagrimaR.x, (int)lagrimaR.y);
          }
          // Desenha boca triste melhorada (só se não desenhou a de fome/bravo)
          int limiarTriste = (personalidade == PERSONALIDADE_SENSIVEL) ? 50 : 30;
          if (fome > limiarTriste && diversao > limiarTriste && saude > limiarTriste) {
            int offsetMouthY = (int)(sin(millis() * 0.0025) * 0.6); // respiração lenta
            u8g2.drawLine(58, 51 + offsetMouthY, 61, 48 + offsetMouthY);
            u8g2.drawLine(61, 48 + offsetMouthY, 67, 48 + offsetMouthY);
            u8g2.drawLine(67, 48 + offsetMouthY, 70, 51 + offsetMouthY);
          }
        }
      }
      
      // Desenha o gatinho mini no canto inferior da tela se a etiqueta estiver presente
      if (petTagPresente && !animacaoAcariciado) {
        if (luzPorcento < 10) {
          // Gatinho dormindo
          u8g2.drawDisc(112, 58, 4);
          u8g2.drawPixel(107, 57); // orelha 1
          u8g2.drawPixel(109, 55); // orelha 2
          
          // Letrinhas zZz subindo
          int zOffset = (millis() / 400) % 3;
          u8g2.setFont(u8g2_font_profont11_tr);
          if (zOffset == 0) u8g2.drawStr(118, 54, "z");
          else if (zOffset == 1) u8g2.drawStr(120, 50, "z");
          else if (zOffset == 2) u8g2.drawStr(122, 46, "Z");
        } else {
          // Gatinho andando (apenas desenha, física atualizada fora do loop de renderização)
          int cx = (int)catMiniX;
          int cy = 54;
          u8g2.setFont(u8g2_font_profont11_tr);
          if (catMiniDirRight) {
            // Corpo
            u8g2.drawBox(cx, cy, 8, 5);
            // Cabeça
            u8g2.drawBox(cx + 6, cy - 2, 4, 4);
            // Orelhas
            u8g2.drawPixel(cx + 7, cy - 3);
            u8g2.drawPixel(cx + 9, cy - 3);
            // Rabo
            u8g2.drawLine(cx, cy, cx - 2, cy - 2);
            // Pernas
            if (((millis() / 150) % 2) == 0) {
              u8g2.drawLine(cx + 1, cy + 5, cx + 1, cy + 7);
              u8g2.drawLine(cx + 6, cy + 5, cx + 6, cy + 7);
            } else {
              u8g2.drawLine(cx + 2, cy + 5, cx + 2, cy + 7);
              u8g2.drawLine(cx + 5, cy + 5, cx + 5, cy + 7);
            }
          } else {
            // Corpo
            u8g2.drawBox(cx + 2, cy, 8, 5);
            // Cabeça
            u8g2.drawBox(cx, cy - 2, 4, 4);
            // Orelhas
            u8g2.drawPixel(cx, cy - 3);
            u8g2.drawPixel(cx + 2, cy - 3);
            // Rabo
            u8g2.drawLine(cx + 9, cy, cx + 11, cy - 2);
            // Pernas
            if (((millis() / 150) % 2) == 0) {
              u8g2.drawLine(cx + 3, cy + 5, cx + 3, cy + 7);
              u8g2.drawLine(cx + 8, cy + 5, cx + 8, cy + 7);
            } else {
              u8g2.drawLine(cx + 4, cy + 5, cx + 4, cy + 7);
              u8g2.drawLine(cx + 7, cy + 5, cx + 7, cy + 7);
            }
          }
        }
      }
    }
  } while ( u8g2.nextPage() );

  // Ajustado o delay para manter as transições fluidas (~30 FPS)
  delay(35); 
}
