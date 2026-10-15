#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <esp_now.h>

enum EstadoTela {
  TELA_FACE,
  TELA_TEMP,
  TELA_UMID,
  TELA_CLOCK,
  TELA_IDADE,
  TELA_PET_STATUS
};

Preferences preferences;
WebServer server(80);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
long distancia = 999;

// =========================================================================
// === CONFIGURAÇÃO DE TEMPOS DO TAMAGOTCHI (Edite aqui para alterar) ===
// =========================================================================
const int TEMPO_CHOCAR_OVO_SEGUNDOS = 15; // Tempo padrão para testes (15s)
// =========================================================================
// === CONFIGURAÇÃO DE REDE WI-FI E NTP (Ajuste para sua rede) ===
// =========================================================================
const char* ssid = "Orion";
const char* password = "OptimusPrime021";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3 * 3600; // GMT-3 (Horário de Brasília)
const int daylightOffset_sec = 0;
// Defina os tempos abaixo em milissegundos (ex: 1000 = 1 segundo)
const unsigned long INTERVALO_FOME = 99999;    // Fome decai a cada 2 segundos (rápido para testes)
const unsigned long INTERVALO_BRINCAR = 99999; // Diversão decai a cada 3 segundos (rápido para testes)
const unsigned long INTERVALO_SAUDE = 99999;   // Saúde decai a cada 4 segundos (rápido para testes)
const unsigned long INTERVALO_ALERTA = 60000; // Buzzer apita a cada 10 segundos se negligenciado (rápido para testes)
const int TEMPO_TELA_INFO = 4000;             // Tempo de permanência de cada tela de status (4s)
const unsigned long DURACAO_REACAO = 6000;    // Duração das animações de reação (comer, brincar, curar)

// --- CONFIGURAÇÃO DO TEMPO DE INCUBAÇÃO DO OVO ---
unsigned int tempoIncubacaoSegundos = 15; // Tempo para o ovo chocar em segundos (padrão: 900 para 15 minutos)

// Variáveis globais para bufferizar RFID enviadas ao ESP-NOW
bool rfidLidoPendente = false;
byte rfidUIDPendente[4] = {0, 0, 0, 0};
uint8_t rfidAcaoPendente = 0;
uint16_t somMaxDevGlobal = 0;

// --- CONFIGURAÇÃO DE LÓGICA DO SENSOR DE SOM ---
// Defina como true se o sensor envia nível LOW quando detecta barulho (Padrão LM393/KY-038)
// Defina como false se o sensor envia nível HIGH quando detecta barulho
const bool SENSOR_SOM_ATIVO_LOW = true; 
// =========================================================================

// --- CONFIGURAÇÕES DOS SENSORES (MIGRADO PARA ESP32-S3) ---
#define DHTPIN 17       
#define DHTTYPE DHT11  
DHT dht(DHTPIN, DHTTYPE);

const int obstaculoPin = 15; // Sensor de Obstáculo IR (Digital OUT)
const int choquePin = 16;    // Sensor de Choque/Vibração (Digital OUT) - Reutiliza antigo pino Echo 
const int ldrPin = 1; // GPIO 1 (ADC1_CH0)
const int botaoPin = 21;
const int buzzerPin = 18;
const int somAnalogPin = 2; // GPIO 2 (ADC1_CH1)
// Modo silencioso: true = sem som no buzzer
bool modoSilencioso = false;

// Feedback temporário na tela para o modo silencioso
unsigned long tempoFimFeedbackSilencioso = 0;
String feedbackSilenciosoMsg = "";

// Toca uma nota usando LEDC com 50% de duty cycle (volume máximo físico do buzzer) se não estiver silencioso
// Substitui tone() + noTone() para compatibilidade com ESP32 Core v3.0+
void myTone(int freq, long dur_ms = 0) {
  if (modoSilencioso || freq == 0) {
    ledcWrite(buzzerPin, 0);
    return;
  }
  // Altera a frequência do timer associado ao pino com resolução de 8 bits
  ledcChangeFrequency(buzzerPin, freq, 8);
  // Escreve 50% de duty cycle (128 de 256) para som normal
  ledcWrite(buzzerPin, 128);
  if (dur_ms > 0) {
    delay(dur_ms);
    myNoTone(); // Silencia o buzzer após a duração especificada
  }
}

void myNoTone() {
  ledcWrite(buzzerPin, 0);
}

// LIMIARES CONFIGURÁVEIS (Valor padrão inicial, salvos no NVS)
int limiarBrincar = 130;  // Som baixo / voz normal / fala (0-4095)
int limiarSusto = 700;    // Som forte / grito / palma alta (0-4095)
int limiarLuzBaixo = 10;
int limiarLuzAlto = 80;
float limiarTempBaixo = 25.0;
float limiarTempAlto = 29.0;

// --- CONFIGURAÇÃO DO LEITOR RFID (MIGRADO PARA ESP32-S3) ---
#define SS_PIN 10
#define RST_PIN 14
MFRC522 rfid(SS_PIN, RST_PIN);

// --- VARIÁVEIS GLOBAIS ---
unsigned long tempoUltimaLeituraDHT = 0;
float temperatura = 26; // Começa com um valor normal
float umidade = 0;
int ldrValor = 0;
int luzPorcento = 100;
bool wifiConectado = false;
bool petDormindo = false;

// Nome do Robô e Configuração de Persistência
String nomeRobo = "Robo";
unsigned long segundosDeVida = 0;
unsigned int idadeDias = 0;
unsigned long tempoUltimoSegundoVida = 0;

// Variáveis para Habilidades Desbloqueáveis
bool animacaoHighFive = false;
unsigned long tempoInicioHighFive = 0;
bool animacaoDance = false;
unsigned long tempoInicioDance = 0;
bool animacaoDado = false;
unsigned long tempoInicioDado = 0;
int valorDado = 1;
bool modoSentinela = false;
bool modoPiano = false;
bool sonoAutomaticoHorario = true;
unsigned long tempoProximaFala = 15000;
unsigned long tempoFimFala = 0;
String textoFala = "";
String jsonSkills = "";
String jsonFrases = "{\"sarcastica\":{\"doente\":[\"VOU MORRER DE FOME.\",\"CADE MEU PRATO?\",\"NADA DE COMIDA?\"],\"entediado\":[\"QUE CHATICE.\",\"NADA PRA FAZER.\",\"SAI DAQUI.\",\"QUE SACO.\"],\"feliz\":[\"NAO ENCHE.\",\"OK, TANTO FAZ.\",\"TA BOM, VALEU.\",\"AHAM, SERTO.\"]},\"sensivel\":{\"doente\":[\"FOME... CHORANDO...\",\"DODOI...\",\"ME AJUDA...\",\"ESTOU MAL...\"],\"entediado\":[\"BRINCA COMIGO?\",\"QUERO DIVERSAO...\",\"SOLITARIO...\",\"ONDE VOCE ESTA?\"],\"feliz\":[\"AMO VOCE!\",\"LINDO DIA!\",\"MUITO FELIZ!\",\"QUE MARAVILHA!\"]},\"normal\":{\"doente\":[\"FOME POR FAVOR\",\"DODOI...\",\"PRECISO DE ATENCAO\",\"ESTOU FRACO...\"],\"entediado\":[\"VAMOS BRINCAR?\",\"QUE TEDIO...\",\"QUERO UM JOGO\",\"ME ENTRETÉM!\"],\"feliz\":[\"BIPI BIP!\",\"OLHO AO REDOR\",\"FELIZ!\",\"TUDO BEM!\"]}}";

bool forcarChoro = false;
unsigned long tempoFimForcarChoro = 0;
bool forcarOculos = false;
unsigned long tempoFimForcarOculos = 0;
unsigned long tempoFimTesteFace = 0;
unsigned long tempoUltimoChoque = 0;

EstadoTela telaAtual = TELA_FACE;
unsigned long tempoBotaoPressionado = 0;

// Variáveis de Humor e Micro-Animações Ociosas
enum HumorPet {
  HUMOR_FELIZ,
  HUMOR_ENTEDIADO,
  HUMOR_CARENTE,
  HUMOR_DOENTE
};
HumorPet humorAtual = HUMOR_FELIZ;

unsigned long tempoProximaReacaoOciosa = 0;
unsigned long tempoFimReacaoOciosa = 0;
int tipoReacaoOciosa = 0; // 0 = nenhuma, 1 = assobio, 2 = revirar olhos, 3 = suspiro, 4 = tremedeira, 5 = espirro, 6 = bocejo
float ociosoLookX = 0.0;
float ociosoLookY = 0.0;

// Variáveis para Transição Suave de Tela (Slide Lateral)
bool emTransicao = false;
unsigned long tempoInicioTransicao = 0;
const unsigned long DURACAO_TRANSICAO = 400; // milissegundos
EstadoTela telaOrigem = TELA_FACE;
EstadoTela telaDestino = TELA_FACE;

void mudarTelaPara(EstadoTela novaTela) {
  if (telaAtual == novaTela || emTransicao) return;
  telaOrigem = telaAtual;
  telaDestino = novaTela;
  emTransicao = true;
  tempoInicioTransicao = millis();
}

// Forward declarations para compilar corretamente
void drawHeart(int x, int y);
void drawLargeHeart(int x, int y, bool small = false);
void drawEyeHappy(float x, float y, float w, float h, float r, float progress);
void drawEyeSqueezed(float x, float y, float w, float h, float r, float progress, bool isLeft);


// --- SISTEMA DE PARTÍCULAS COM FÍSICA SUAVE ---
struct ParticulaFisica {
  float x;
  float y;
  float vx;
  float vy;
  char tipo; // 'H' = Heart, '+' = Heal, '*' = Food, 'Z' = zZz, 'S' = Sweat, 'L' = Tear
  int vida;
  int maxVida;
  bool ativa;
};
const int MAX_PARTICULAS_FISICAS = 15;
ParticulaFisica particulasFisicas[MAX_PARTICULAS_FISICAS];

void inicializarParticulas() {
  for (int i = 0; i < MAX_PARTICULAS_FISICAS; i++) {
    particulasFisicas[i].ativa = false;
  }
}

void criarParticula(float x, float y, float vx, float vy, char tipo, int maxVida) {
  for (int i = 0; i < MAX_PARTICULAS_FISICAS; i++) {
    if (!particulasFisicas[i].ativa) {
      particulasFisicas[i].x = x;
      particulasFisicas[i].y = y;
      particulasFisicas[i].vx = vx;
      particulasFisicas[i].vy = vy;
      particulasFisicas[i].tipo = tipo;
      particulasFisicas[i].vida = maxVida;
      particulasFisicas[i].maxVida = maxVida;
      particulasFisicas[i].ativa = true;
      break;
    }
  }
}

void atualizarParticulas() {
  for (int i = 0; i < MAX_PARTICULAS_FISICAS; i++) {
    if (particulasFisicas[i].ativa) {
      particulasFisicas[i].x += particulasFisicas[i].vx;
      particulasFisicas[i].y += particulasFisicas[i].vy;
      
      // Física específica por tipo
      if (particulasFisicas[i].tipo == 'S' || particulasFisicas[i].tipo == 'L') {
        particulasFisicas[i].vy += 0.04; // Gravidade
      } else if (particulasFisicas[i].tipo == 'Z') {
        particulasFisicas[i].vx += (sin(millis() * 0.015) * 0.03); // Vento horizontal senoidal
      } else {
        particulasFisicas[i].vy -= 0.025; // Flutuação para cima
        particulasFisicas[i].vx += (sin(millis() * 0.02 + i) * 0.06); // Oscilação lateral
      }
      
      particulasFisicas[i].vida--;
      if (particulasFisicas[i].vida <= 0 || particulasFisicas[i].y < 0 || particulasFisicas[i].y > 64 || particulasFisicas[i].x < 0 || particulasFisicas[i].x > 128) {
        particulasFisicas[i].ativa = false;
      }
    }
  }
}

void desenharParticulas(int xOffset) {
  for (int i = 0; i < MAX_PARTICULAS_FISICAS; i++) {
    if (particulasFisicas[i].ativa) {
      int px = (int)particulasFisicas[i].x + xOffset;
      int py = (int)particulasFisicas[i].y;
      
      if (particulasFisicas[i].tipo == 'H') {
        drawHeart(px, py);
      }
      else if (particulasFisicas[i].tipo == '+') {
        u8g2.setFont(u8g2_font_profont11_tr);
        u8g2.drawStr(px, py, "+");
      }
      else if (particulasFisicas[i].tipo == '*') {
        u8g2.setFont(u8g2_font_profont11_tr);
        u8g2.drawStr(px, py, "*");
      }
      else if (particulasFisicas[i].tipo == 'Z') {
        u8g2.setFont(u8g2_font_profont11_tr);
        float t = (float)(particulasFisicas[i].maxVida - particulasFisicas[i].vida) / particulasFisicas[i].maxVida;
        if (t < 0.4) {
          u8g2.drawStr(px, py, "z");
        } else {
          u8g2.drawStr(px, py, "Z");
        }
      }
      else if (particulasFisicas[i].tipo == 'S') {
        u8g2.drawBox(px, py, 2, 3);
      }
      else if (particulasFisicas[i].tipo == 'L') {
        u8g2.drawFilledEllipse(px, py, 2, 3);
        u8g2.drawLine(px, py - 2, px, py);
      }
      else if (particulasFisicas[i].tipo == 'M') {
        // Nota musical procedural
        u8g2.drawDisc(px, py, 2);
        u8g2.drawLine(px + 1, py - 5, px + 1, py);
        u8g2.drawLine(px + 1, py - 5, px + 3, py - 4);
      }
      else if (particulasFisicas[i].tipo == 'P') {
        // Sopros/Suspiros
        u8g2.setFont(u8g2_font_profont11_tr);
        u8g2.drawStr(px, py, "~");
      }
    }
  }
}

// =========================================================================
// === RTTTL MELODIES & DECODER ENGINE ===
// =========================================================================
const char* rtttl_mario = "mario:d=4,o=5,b=125:16e6,16e6,32p,8e6,16c6,8e6,8g6,8p,8g5";
const char* rtttl_win = "win:d=4,o=5,b=150:16c,16c,16c,8c,16p,16a#,16b,8c6,16p,16b,8c6";
const char* rtttl_sad = "sad:d=4,o=5,b=120:8g,8g,8g,16d#,32a#,8g,16d#,32a#,2g";

// Novas melodias RTTTL personalizadas para ações e reações
const char* rtttl_feed = "feed:d=8,o=6,b=150:16c,16e,16g,8c7";
const char* rtttl_play = "play:d=16,o=6,b=180:c,e,g,c7,g,c7";
const char* rtttl_cure = "cure:d=16,o=6,b=140:c,g,c7,g,c7,e7,g7,c8";
const char* rtttl_sick_melody = "sick:d=8,o=5,b=90:f,e,d#,d";
const char* rtttl_touch_melody = "purr:d=16,o=6,b=160:c,e,g,b,c7,b,g,e";

// ---- PLAYER RTTTL NÃO-BLOQUEANTE ----
// Estado global do player — avança uma nota por tick de loop()
struct RTTTLPlayer {
  const char* pos;       // posição atual na string
  int default_dur;
  int default_oct;
  int bpm;
  long wholenote;
  unsigned long nextNoteAt; // millis() quando a próxima nota deve ser disparada
  unsigned long silenceAt;  // millis() quando a nota atual deve ser desligada (staccato)
  bool playing;
} rtttlPlayer = {nullptr, 4, 6, 63, 0, 0, 0, false};

// Pré-processa o cabeçalho RTTTL e posiciona o ponteiro nas notas
void startRTTTL(const char* p) {
  if (!p) return;
  rtttlPlayer.default_dur = 4;
  rtttlPlayer.default_oct = 6;
  rtttlPlayer.bpm = 63;

  while (*p && *p != ':') p++;
  if (*p == ':') p++;

  while (*p && *p != ':') {
    char ctrl = *p++;
    if (*p == '=') p++;
    int num = 0;
    while (*p >= '0' && *p <= '9') num = num * 10 + (*p++ - '0');
    if (ctrl == 'd') rtttlPlayer.default_dur = num;
    else if (ctrl == 'o') rtttlPlayer.default_oct = num;
    else if (ctrl == 'b') rtttlPlayer.bpm = num;
    if (*p == ',') p++;
  }
  if (*p == ':') p++;

  rtttlPlayer.wholenote = 240000L / rtttlPlayer.bpm;
  rtttlPlayer.pos = p;
  rtttlPlayer.nextNoteAt = millis();
  rtttlPlayer.silenceAt = 0;
  rtttlPlayer.playing = true;
}

// Chamado a cada loop() — dispara apenas a nota certa no momento certo, sem bloquear
void tickRTTTL() {
  if (!rtttlPlayer.playing) return;

  // Desliga o som da nota atual se chegou o momento de silêncio (staccato)
  if (rtttlPlayer.silenceAt > 0 && millis() >= rtttlPlayer.silenceAt) {
    myNoTone();
    rtttlPlayer.silenceAt = 0;
  }

  if (millis() < rtttlPlayer.nextNoteAt) return;

  const char* p = rtttlPlayer.pos;
  if (!p || !*p) {
    rtttlPlayer.playing = false;
    myNoTone();
    return;
  }

  // Duração
  int num = 0;
  while (*p >= '0' && *p <= '9') num = num * 10 + (*p++ - '0');
  long duration = num ? rtttlPlayer.wholenote / num : rtttlPlayer.wholenote / rtttlPlayer.default_dur;

  // Nota
  int note = 0;
  char nc = *p++;
  if (nc >= 'a' && nc <= 'g') nc -= 32;
  switch (nc) {
    case 'C': note = 1; break; case 'D': note = 3; break;
    case 'E': note = 5; break; case 'F': note = 6; break;
    case 'G': note = 8; break; case 'A': note = 10; break;
    case 'B': note = 12; break; default: note = 0; break;
  }

  if (*p == '#') { note++; p++; }
  if (*p == '.') { duration += duration / 2; p++; }

  int scale = (*p >= '0' && *p <= '9') ? (*p++ - '0') : rtttlPlayer.default_oct;
  if (*p == ',') p++;

  if (note) {
    float freq = 0;
    switch (note) {
      case 1: freq=261.63; break; case 2: freq=277.18; break;
      case 3: freq=293.66; break; case 4: freq=311.13; break;
      case 5: freq=329.63; break; case 6: freq=349.23; break;
      case 7: freq=369.99; break; case 8: freq=392.00; break;
      case 9: freq=415.30; break; case 10: freq=440.00; break;
      case 11: freq=466.16; break; case 12: freq=493.88; break;
    }
    if (scale > 4) freq *= (1 << (scale - 4));
    else if (scale < 4) freq /= (1 << (4 - scale));
    
    // Toca a nota sem bloquear a execução principal (dur_ms = 0)
    myTone((int)freq, 0);
    // Agenda o momento de silenciar a nota (90% do tempo de nota)
    rtttlPlayer.silenceAt = millis() + (long)(duration * 0.9);
  } else {
    myNoTone();
    rtttlPlayer.silenceAt = 0;
  }

  rtttlPlayer.pos = p;
  rtttlPlayer.nextNoteAt = millis() + duration;
}

// Mantém o nome original para compatibilidade com todos os call sites existentes
void playRTTTL(const char* p) {
  startRTTTL(p);
}

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
bool animacaoIrritado = false;
unsigned long tempoInicioReacao = 0;
unsigned long tempoInicioSusto = 0;
unsigned long tempoInicioIrritado = 0;
const unsigned long DURACAO_SUSTO = 2000; // O susto dura 2s
const unsigned long DURACAO_IRRITADO = 2000; // A irritação dura 2s


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
Personalidade personalidadeNatural = PERSONALIDADE_SARCASTICA;
int pontosDeVinculo = 0;
unsigned long tempoUltimaVerificacaoVinculo = 0;

// --- VARIÁVEL GLOBAL PARA VIBRAÇÃO (ANIMAÇÃO DE TONTURA) ---
uint32_t variacaoMovimento = 0; 
bool choqueDetectado = false; // Trava para exibir vibrações rápidas no Monitor Serial 

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
  myTone(880, 80); delay(120);
  myTone(1047, 180); delay(2000); // 2 segundos para o usuário ler a mensagem
}

// --- CONFIGURAÇÃO DE PREFERENCES PARA PERSISTÊNCIA NA FLASH (NVS) ---
void salvarEstado() {
  preferences.begin("tamagotchi", false);
  preferences.putUChar("fome", (byte)fome);
  preferences.putUChar("diversao", (byte)diversao);
  preferences.putUChar("saude", (byte)saude);
  preferences.putBool("estaDoente", estaDoente);
  
  preferences.putBytes("uidComida", uidComida, 4);
  preferences.putBytes("uidBrincar", uidBrincar, 4);
  preferences.putBytes("uidSaude", uidSaude, 4);
  preferences.putBytes("uidPet", uidPet, 4);
  
  preferences.putUChar("estNasc", (byte)estadoNascimento);
  preferences.putUShort("segChoc", (unsigned short)segundosChocados);
  preferences.putUChar("personalidade", (byte)personalidadeNatural);
  preferences.putUChar("vinculo", (byte)pontosDeVinculo);
  
  // Persiste o nome do robô
  preferences.putString("nomeRobo", nomeRobo);
  
  // Persiste os limiares configuráveis
  preferences.putUShort("limPlay", limiarBrincar);
  preferences.putUShort("limSusto", limiarSusto);
  preferences.putUChar("limLuzB", limiarLuzBaixo);
  preferences.putUChar("limLuzA", limiarLuzAlto);
  preferences.putFloat("limTempB", limiarTempBaixo);
  preferences.putFloat("limTempA", limiarTempAlto);
  preferences.putUShort("timeInc", tempoIncubacaoSegundos);
  
  // Persiste os segundos de vida
  preferences.putUInt("segVida", (uint32_t)segundosDeVida);
  
  // Persiste os modos sentinela, piano e as habilidades JSON
  preferences.putBool("sentinela", modoSentinela);
  preferences.putBool("piano", modoPiano);
  preferences.putBool("sonoAuto", sonoAutomaticoHorario);
  preferences.putBool("silencioso", modoSilencioso);
  preferences.putString("jsonSkills", jsonSkills);
  preferences.putString("jsonFrases", jsonFrases);
  
  preferences.end();
}

void carregarEstado() {
  preferences.begin("tamagotchi", false);
  
  if (preferences.isKey("fome")) {
    fome = preferences.getUChar("fome");
    diversao = preferences.getUChar("diversao");
    saude = preferences.getUChar("saude");
    estaDoente = preferences.getBool("estaDoente");
    
    preferences.getBytes("uidComida", uidComida, 4);
    preferences.getBytes("uidBrincar", uidBrincar, 4);
    preferences.getBytes("uidSaude", uidSaude, 4);
    preferences.getBytes("uidPet", uidPet, 4);
    
    estadoNascimento = (EstadoNascimento)preferences.getUChar("estNasc");
    segundosChocados = preferences.getUShort("segChoc");
    personalidadeNatural = (Personalidade)preferences.getUChar("personalidade", (byte)PERSONALIDADE_SARCASTICA);
    personalidade = personalidadeNatural;
    pontosDeVinculo = preferences.getUChar("vinculo");
    
    // Carrega o nome do robô
    if (preferences.isKey("nomeRobo")) {
      nomeRobo = preferences.getString("nomeRobo");
    } else {
      nomeRobo = "Robo";
    }
    
    // Carrega os limiares configuráveis se existirem
    if (preferences.isKey("limPlay")) limiarBrincar = preferences.getUShort("limPlay");
    if (preferences.isKey("limSusto")) limiarSusto = preferences.getUShort("limSusto");
    if (limiarBrincar <= 0) limiarBrincar = 130;
    if (limiarSusto <= 0) limiarSusto = 700;
    if (preferences.isKey("limLuzB")) limiarLuzBaixo = preferences.getUChar("limLuzB");
    if (preferences.isKey("limLuzA")) limiarLuzAlto = preferences.getUChar("limLuzA");
    if (preferences.isKey("limTempB")) limiarTempBaixo = preferences.getFloat("limTempB");
    if (preferences.isKey("limTempA")) limiarTempAlto = preferences.getFloat("limTempA");
    if (preferences.isKey("timeInc")) tempoIncubacaoSegundos = preferences.getUShort("timeInc");
    
    // Carrega os segundos de vida
    segundosDeVida = preferences.getUInt("segVida", 0);
    idadeDias = segundosDeVida / 86400;
    
    // Carrega os modos e habilidades
    modoSentinela = preferences.getBool("sentinela", false);
    modoPiano = preferences.getBool("piano", false);
    sonoAutomaticoHorario = preferences.getBool("sonoAuto", true);
    modoSilencioso = preferences.getBool("silencioso", false);
    if (preferences.isKey("jsonSkills")) {
      jsonSkills = preferences.getString("jsonSkills");
    } else {
      // Inicializa com o JSON padrão das habilidades
      jsonSkills = F("[{\"day\":1,\"name\":\"Assobiar Super Mario\",\"desc\":\"O robo aprende a assobiar o classico tema do Super Mario Bros no buzzer.\"},"
                     "{\"day\":2,\"name\":\"High-Five IR\",\"desc\":\"Usando o sensor de obstaculo IR, aproxime a mao a menos de 5cm para bater a mao com o pet.\"},"
                     "{\"day\":3,\"name\":\"Danca com Palmas\",\"desc\":\"O sensor de som detecta barulho ritmado e o robo pisca os olhos em ritmo de festa.\"},"
                     "{\"day\":4,\"name\":\"Previsao de Clima\",\"desc\":\"O dht11 detecta umidade alta (>80%) e avisa ativamente na tela: cuidado com o mofo!\"},"
                     "{\"day\":5,\"name\":\"Rolador de Dados\",\"desc\":\"Ao chacoalhar o robo, o sensor de choque/vibração faz ele rodar um dado na tela (1 a 6).\"},"
                     "{\"day\":7,\"name\":\"Estilo Sarcastico\",\"desc\":\"Desbloqueia oculos escuros 'Deal With It' caindo na tela no modo ocioso da Sarcastica.\"},"
                     "{\"day\":10,\"name\":\"Modo Sentinela\",\"desc\":\"Ative o sensor de obstaculo como alarme de presenca que apita se alguem passar na frente.\"},"
                     "{\"day\":12,\"name\":\"Ventilador no Calor\",\"desc\":\"Ao passar de 32C, exibe um ventilador girando na tela para se refrescar.\"},"
                     "{\"day\":14,\"name\":\"Sinergia de Gatinho RFID\",\"desc\":\"Aproximar a tag do gatinho mostra os dois brincando juntos em tela dividida.\"},"
                     "{\"day\":15,\"name\":\"Falas de Personalidade\",\"desc\":\"O robo ganha voz e exibe caixas de dialogo na tela com mensagens baseadas em humor.\"},"
                     "{\"day\":21,\"name\":\"Teclado Piano no Botao\",\"desc\":\"O botao fisico vira um piano seletor no buzzer (cliques curtos/longos emitem notas).\"},"
                     "{\"day\":30,\"name\":\"Meditacao Zen\",\"desc\":\"Seu pet aprende a economizar energia e seus atributos decaem 30% mais devagar.\"}]");
    }
    if (preferences.isKey("jsonFrases")) {
      jsonFrases = preferences.getString("jsonFrases");
    }
    
    Serial.println(F("Estado carregado da Flash NVS com sucesso!"));
    Serial.print(F("Nome do Robo: "));
    Serial.println(nomeRobo);
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
    Serial.println(F("NVS Flash vazia. Inicializando valores padrao de nascimento (OVO)..."));
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
    personalidadeNatural = PERSONALIDADE_SARCASTICA;
    pontosDeVinculo = 0;
    nomeRobo = "Robo";
    segundosDeVida = 0;
    idadeDias = 0;
    modoSentinela = false;
    modoPiano = false;
    jsonSkills = F("[{\"day\":1,\"name\":\"Assobiar Super Mario\",\"desc\":\"O robo aprende a assobiar o classico tema do Super Mario Bros no buzzer.\"},"
                   "{\"day\":2,\"name\":\"High-Five IR\",\"desc\":\"Usando o sensor de obstaculo IR, aproxime a mao a menos de 5cm para bater a mao com o pet.\"},"
                   "{\"day\":3,\"name\":\"Danca com Palmas\",\"desc\":\"O sensor de som detecta barulho ritmado e o robo pisca os olhos em ritmo de festa.\"},"
                   "{\"day\":4,\"name\":\"Previsao de Clima\",\"desc\":\"O dht11 detecta umidade alta (>80%) e avisa ativamente na tela: cuidado com o mofo!\"},"
                   "{\"day\":5,\"name\":\"Rolador de Dados\",\"desc\":\"Ao chacoalhar o robo, o sensor de choque/vibração faz ele rodar um dado na tela (1 a 6).\"},"
                   "{\"day\":7,\"name\":\"Estilo Sarcastico\",\"desc\":\"Desbloqueia oculos escuros 'Deal With It' caindo na tela no modo ocioso da Sarcastica.\"},"
                   "{\"day\":10,\"name\":\"Modo Sentinela\",\"desc\":\"Ative o sensor de obstaculo como alarme de presenca que apita se alguem passar na frente.\"},"
                   "{\"day\":12,\"name\":\"Ventilador no Calor\",\"desc\":\"Ao passar de 32C, exibe um ventilador girando na tela para se refrescar.\"},"
                   "{\"day\":14,\"name\":\"Sinergia de Gatinho RFID\",\"desc\":\"Aproximar a tag do gatinho mostra os dois brincando juntos em tela dividida.\"},"
                   "{\"day\":15,\"name\":\"Falas de Personalidade\",\"desc\":\"O robo ganha voz e exibe caixas de dialogo na tela com mensagens baseadas em humor.\"},"
                   "{\"day\":21,\"name\":\"Teclado Piano no Botao\",\"desc\":\"O botao fisico vira um piano seletor no buzzer (cliques curtos/longos emitem notas).\"},"
                   "{\"day\":30,\"name\":\"Meditacao Zen\",\"desc\":\"Seu pet aprende a economizar energia e seus atributos decaem 30% mais devagar.\"}]");
    limiarBrincar = 130;
    limiarSusto = 700;
    limiarLuzBaixo = 10;
    limiarLuzAlto = 80;
    limiarTempBaixo = 25.0;
    limiarTempAlto = 29.0;
    tempoIncubacaoSegundos = 15;
    preferences.end(); // Fecha antes de salvar estado
    salvarEstado();
    return;
  }
  preferences.end();
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
  personalidadeNatural = PERSONALIDADE_SARCASTICA;
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

  // Melodia alegre especial de nascimento (RTTTL)
  playRTTTL(rtttl_mario);
  
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
      if (segundosChocados >= tempoIncubacaoSegundos) {
        nascerPet();
        return; // Sai do fluxo de incubação já que nasceu
      }
    }
  }

  // Leitura do RFID para chocar o ovo
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    rfidLidoPendente = true;
    memcpy(rfidUIDPendente, rfid.uid.uidByte, 4);
    
    if (estadoNascimento == ESTADO_OVO) {
      estadoNascimento = ESTADO_CHOCANDO;
      segundosChocados = 0;
      tempoUltimoSegundoChocado = millis();
      salvarEstado();
      
      // Toque melódico alegre para início da incubação
      myTone(523, 100); delay(120);
      myTone(659, 100); delay(120);
      myTone(784, 250);
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
      if (segundosChocados >= (tempoIncubacaoSegundos * 2) / 3) stage = 3;      // Último terço do tempo
      else if (segundosChocados >= tempoIncubacaoSegundos / 3) stage = 2; // Segundo terço do tempo
      
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
      int progressWidth = map(segundosChocados, 0, tempoIncubacaoSegundos, 0, 76);
      if (progressWidth > 76) progressWidth = 76;
      u8g2.drawBox(26, 50, progressWidth, 1);
      
      // Tempo restante (MM:SS)
      unsigned int tempoRestante = tempoIncubacaoSegundos - segundosChocados;
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
  Personalidade antigaPersNat = personalidadeNatural;
  if (pontosDeVinculo < 40) {
    personalidadeNatural = PERSONALIDADE_SARCASTICA;
  } else if (pontosDeVinculo < 80) {
    personalidadeNatural = PERSONALIDADE_BASICA;
  } else {
    personalidadeNatural = PERSONALIDADE_SENSIVEL;
  }
  
  // Se a personalidade evoluiu ou regrediu
  if (personalidadeNatural != antigaPersNat) {
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
      if (personalidadeNatural > antigaPersNat) {
        u8g2.drawStr(12, 33, "EVOLUIU! :)");
      } else {
        u8g2.drawStr(12, 33, "REGREDIU... :(");
      }
      u8g2.setFont(u8g2_font_profont12_tf);
      
      if (personalidadeNatural == PERSONALIDADE_SARCASTICA) {
        Serial.println(F("SARCASTICA"));
        u8g2.drawStr(12, 52, "SARCASTICA");
      } else if (personalidadeNatural == PERSONALIDADE_BASICA) {
        Serial.println(F("BASICA"));
        u8g2.drawStr(12, 52, "BASICA (NORMAL)");
      } else if (personalidadeNatural == PERSONALIDADE_SENSIVEL) {
        Serial.println(F("SENSIVEL"));
        u8g2.drawStr(12, 52, "SENSIVEL (CARENTE)");
      }
    } while (u8g2.nextPage());
    Serial.println(F("============================================="));
    
    // Toca som correspondente (RTTTL)
    if (personalidadeNatural > antigaPersNat) {
      // Evolução: melodia feliz (RTTTL)
      playRTTTL(rtttl_win);
    } else {
      // Regressão: melodia triste (RTTTL)
      playRTTTL(rtttl_sad);
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
  animacaoAcariciado = false;
  animacaoIrritado = false;
  tipoReacaoOciosa = 0;
  tempoProximaReacaoOciosa = millis() + random(25000, 50000);
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  // Melodia RTTTL de alimentação de acordo com a personalidade
  if (personalidade == PERSONALIDADE_SARCASTICA) {
    playRTTTL("sarc_feed:d=8,o=5,b=100:16g,16f");
  } else if (personalidade == PERSONALIDADE_SENSIVEL) {
    playRTTTL("sens_feed:d=16,o=6,b=140:e,g,b,e7");
  } else {
    playRTTTL(rtttl_feed);
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
  animacaoAcariciado = false;
  animacaoIrritado = false;
  tipoReacaoOciosa = 0;
  tempoProximaReacaoOciosa = millis() + random(25000, 50000);
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  if (tocarMelodia) {
    if (personalidade == PERSONALIDADE_SARCASTICA) {
      playRTTTL("sarc_play:d=8,o=5,b=120:16f,16f#");
    } else if (personalidade == PERSONALIDADE_SENSIVEL) {
      playRTTTL("sens_play:d=16,o=6,b=160:c,f,a,c7,a,f");
    } else {
      playRTTTL(rtttl_play);
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
  animacaoAcariciado = false;
  animacaoIrritado = false;
  tipoReacaoOciosa = 0;
  tempoProximaReacaoOciosa = millis() + random(25000, 50000);
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  // Melodia RTTTL de cura de acordo com a personalidade
  if (personalidade == PERSONALIDADE_SARCASTICA) {
    playRTTTL("sarc_cure:d=8,o=5,b=110:16f,16a");
  } else if (personalidade == PERSONALIDADE_SENSIVEL) {
    playRTTTL("sens_cure:d=16,o=6,b=130:g,b,d7,g7");
  } else {
    playRTTTL(rtttl_cure);
  }
  
  atualizarVinculo(5); // Ganha +5 pontos de vínculo por curar
  salvarEstado(); // Grava alteração de atributos na EEPROM
}

void acariciarPet() {
  animacaoComendo = false;
  animacaoBrincando = false;
  animacaoCurando = false;
  animacaoAcariciado = true;
  animacaoIrritado = false;
  tipoReacaoOciosa = 0;
  tempoProximaReacaoOciosa = millis() + random(25000, 50000);
  tempoInicioReacao = millis();
  telaAtual = TELA_FACE;
  
  // Melodia RTTTL de carinho de acordo com a personalidade
  if (personalidade == PERSONALIDADE_SARCASTICA) {
    playRTTTL("sarc_purr:d=16,o=5,b=100:16c,16c#");
  } else if (personalidade == PERSONALIDADE_SENSIVEL) {
    playRTTTL("sens_purr:d=16,o=6,b=120:c,e,g,c7");
  } else {
    playRTTTL(rtttl_touch_melody);
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

void drawEyeHappy(float x, float y, float w, float h, float r, float progress) {
  // Desenha o olho normal primeiro
  drawEye(x, y, w, h, r);
  
  if (progress > 0.0) {
    // Desenha uma elipse preta para cortar a parte inferior do olho
    float maxCutHeight = h * 0.35; // corta até 35% da altura
    float ry = w * 0.6; // raio vertical da elipse
    float cutY = y + h / 2.0 + ry - (progress * maxCutHeight); // centro Y da elipse preta
    float rx = w * 0.75; // raio horizontal
    
    u8g2.setDrawColor(0);
    u8g2.drawFilledEllipse((int)x, (int)cutY, (int)rx, (int)ry);
    u8g2.setDrawColor(1);
  }
}

void drawEyeSqueezed(float x, float y, float w, float h, float r, float progress, bool isLeft) {
  // Desenha o olho normal primeiro
  drawEye(x, y, w, h, r);
  
  if (progress > 0.0) {
    float maxCutWidth = w * 0.45; // corte de meia-lua lateral (45% da largura)
    float rx = w * 0.55; // raio da elipse de corte
    float ry = h * 0.7; // raio vertical para cobrir todo o olho
    
    u8g2.setDrawColor(0);
    
    if (isLeft) {
      // 1. Corte de meia-lua na lateral esquerda (fora para dentro)
      float cutX = x - w / 2.0 - rx + (progress * maxCutWidth);
      u8g2.drawFilledEllipse((int)cutX, (int)y, (int)rx, (int)ry);
      
      // 2. Deformação pontuda na lateral direita (shave top-right e bottom-right)
      int px0_top = (int)(x + w / 2.0 + 1);
      int py0_top = (int)(y - h / 2.0 - 1);
      int px1_top = (int)(x + w / 2.0 - w * 0.5 * progress);
      int py1_top = py0_top;
      int px2_top = px0_top;
      int py2_top = (int)(y - h / 2.0 + h * 0.45 * progress);
      u8g2.drawTriangle(px0_top, py0_top, px1_top, py1_top, px2_top, py2_top);
      
      int px0_bot = (int)(x + w / 2.0 + 1);
      int py0_bot = (int)(y + h / 2.0 + 1);
      int px1_bot = (int)(x + w / 2.0 - w * 0.5 * progress);
      int py1_bot = py0_bot;
      int px2_bot = px0_bot;
      int py2_bot = (int)(y + h / 2.0 - h * 0.45 * progress);
      u8g2.drawTriangle(px0_bot, py0_bot, px1_bot, py1_bot, px2_bot, py2_bot);
    } else {
      // 1. Corte de meia-lua na lateral direita (fora para dentro)
      float cutX = x + w / 2.0 + rx - (progress * maxCutWidth);
      u8g2.drawFilledEllipse((int)cutX, (int)y, (int)rx, (int)ry);
      
      // 2. Deformação pontuda na lateral esquerda (shave top-left e bottom-left)
      int px0_top = (int)(x - w / 2.0 - 1);
      int py0_top = (int)(y - h / 2.0 - 1);
      int px1_top = (int)(x - w / 2.0 + w * 0.5 * progress);
      int py1_top = py0_top;
      int px2_top = px0_top;
      int py2_top = (int)(y - h / 2.0 + h * 0.45 * progress);
      u8g2.drawTriangle(px0_top, py0_top, px1_top, py1_top, px2_top, py2_top);
      
      int px0_bot = (int)(x - w / 2.0 - 1);
      int py0_bot = (int)(y + h / 2.0 + 1);
      int px1_bot = (int)(x - w / 2.0 + w * 0.5 * progress);
      int py1_bot = py0_bot;
      int px2_bot = px0_bot;
      int py2_bot = (int)(y + h / 2.0 - h * 0.45 * progress);
      u8g2.drawTriangle(px0_bot, py0_bot, px1_bot, py1_bot, px2_bot, py2_bot);
    }
    
    u8g2.setDrawColor(1);
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

// Desenha um coração grande (aproximadamente 13x11 pixels) para os olhos da personalidade sensível
void drawLargeHeart(int x, int y, bool small) {
  if (small) {
    u8g2.drawFilledEllipse(x - 2, y - 1, 2, 2);
    u8g2.drawFilledEllipse(x + 2, y - 1, 2, 2);
    u8g2.drawTriangle(x - 4, y, x + 4, y, x, y + 4);
  } else {
    u8g2.drawFilledEllipse(x - 3, y - 2, 3, 3);
    u8g2.drawFilledEllipse(x + 3, y - 2, 3, 3);
    u8g2.drawTriangle(x - 6, y - 1, x + 6, y - 1, x, y + 6);
  }
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



// =========================================================================
// === WEB SERVER HANDLERS ===
// =========================================================================
void handleRoot() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  html += F("<title>Tamagotchi Dashboard</title>");
  html += F("<style>");
  html += F("body { font-family: 'Segoe UI', Arial, sans-serif; background-color: #121212; color: #e0e0e0; margin: 0; padding: 20px; text-align: center; }");
  html += F(".card { background: #1e1e1e; padding: 20px; border-radius: 12px; max-width: 450px; margin: 20px auto; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }");
  html += F("h1 { color: #bb86fc; margin-bottom: 5px; }");
  html += F("p.subtitle { color: #888; font-size: 0.9em; margin-bottom: 20px; }");
  html += F(".tabs { display: flex; justify-content: space-around; margin-bottom: 20px; border-bottom: 1px solid #444; }");
  html += F(".tab-btn { background: none; color: #888; border: none; padding: 10px; cursor: pointer; font-size: 1.0em; transition: 0.3s; border-radius: 0; border-bottom: 2px solid transparent; width: 25%; outline: none; }");
  html += F(".tab-btn:hover { color: #bb86fc; transform: none; }");
  html += F(".tab-btn.active { color: #bb86fc; border-bottom: 2px solid #bb86fc; font-weight: bold; }");
  html += F(".tab-content { display: none; text-align: left; }");
  html += F(".tab-content.active { display: block; }");
  html += F(".stat-row { display: flex; justify-content: space-between; align-items: center; margin: 12px 0; }");
  html += F(".stat-label { font-weight: bold; width: 80px; text-align: left; }");
  html += F(".progress-bg { background: #333; width: 100%; height: 16px; border-radius: 8px; margin: 0 10px; overflow: hidden; }");
  html += F(".progress-bar { height: 100%; border-radius: 8px; transition: width 0.5s ease; }");
  html += F(".bar-fome { background: #ff4757; }");
  html += F(".bar-brincar { background: #1e90ff; }");
  html += F(".bar-saude { background: #2ed573; }");
  html += F(".bar-vinculo { background: #ffa502; }");
  html += F(".btn-container { display: flex; justify-content: space-around; margin-top: 25px; }");
  html += F("button { background: #bb86fc; border: none; color: #121212; padding: 12px 20px; font-size: 1em; font-weight: bold; border-radius: 8px; cursor: pointer; transition: 0.2s; }");
  html += F("button:hover { background: #d7b4ff; transform: scale(1.03); }");
  html += F(".btn-cure { background: #2ed573; } .btn-cure:hover { background: #56eb8f; }");
  html += F(".btn-play { background: #1e90ff; color: #fff; } .btn-play:hover { background: #54a0ff; }");
  html += F(".btn-feed { background: #ff4757; color: #fff; } .btn-feed:hover { background: #ff6b81; }");
  html += F(".env-info { display: flex; justify-content: space-around; background: #292929; padding: 12px; border-radius: 8px; margin-top: 20px; font-size: 0.9em; text-align: center; }");
  html += F(".name-container { display: flex; justify-content: center; gap: 8px; margin-bottom: 20px; }");
  html += F(".input-name { background: #2a2a2a; border: 1px solid #444; border-radius: 6px; padding: 8px 12px; color: #fff; font-size: 0.95em; width: 180px; outline: none; }");
  html += F(".input-name:focus { border-color: #bb86fc; }");
  html += F(".btn-save { padding: 8px 16px; font-size: 0.9em; }");
  
  html += F(".config-group { margin: 18px 0; background: #252525; padding: 12px; border-radius: 8px; }");
  html += F(".config-group > label { display: block; font-size: 0.95em; color: #bb86fc; margin-bottom: 10px; font-weight: bold; }");
  html += F(".config-row { display: flex; gap: 10px; }");
  html += F(".config-field { flex: 1; display: flex; flex-direction: column; }");
  html += F(".config-field label { font-size: 0.8em; color: #aaa; margin-bottom: 4px; }");
  html += F(".config-input { background: #1a1a1a; border: 1px solid #444; border-radius: 6px; padding: 8px; color: #fff; font-size: 0.9em; outline: none; }");
  html += F(".config-input:focus { border-color: #bb86fc; }");
  html += F(".btn-submit { background: #bb86fc; color: #121212; width: 100%; margin-top: 15px; padding: 12px; border-radius: 8px; font-size: 1em; font-weight: bold; }");
  
  // Estilos da aba de habilidades
  html += F(".skill-item { background: #252525; padding: 15px; border-radius: 8px; margin-bottom: 12px; border-left: 4px solid #444; }");
  html += F(".skill-item.unlocked { border-left-color: #2ed573; }");
  html += F(".skill-item.locked { opacity: 0.6; }");
  html += F(".skill-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }");
  html += F(".skill-title { font-weight: bold; font-size: 1.05em; color: #fff; }");
  html += F(".skill-item.unlocked .skill-title { color: #2ed573; }");
  html += F(".skill-badge { background: #444; font-size: 0.8em; padding: 3px 8px; border-radius: 12px; color: #aaa; font-weight: bold; }");
  html += F(".skill-item.unlocked .skill-badge { background: rgba(46, 213, 115, 0.2); color: #2ed573; }");
  html += F(".skill-desc { font-size: 0.9em; color: #ccc; margin-bottom: 6px; }");
  html += F(".skill-status { font-size: 0.8em; font-weight: bold; color: #888; }");
  html += F(".skill-item.unlocked .skill-status { color: #2ed573; }");
  html += F(".btn-test-skill { background: #2ed573; color: #121212; border: none; border-radius: 4px; padding: 4px 8px; font-size: 0.8em; font-weight: bold; cursor: pointer; transition: 0.2s; margin-left: auto; margin-right: 10px; }");
  html += F(".btn-test-skill:hover { background: #56eb8f; transform: scale(1.05); }");
  html += F("</style>");
  
  html += F("<script>");
  // Injeta variáveis dinâmicas no JS
  html += F("var idadeDias = "); html += String(idadeDias); html += F(";");
  html += F("var skillsData = "); html += jsonSkills; html += F(";");
  html += F("var frasesData = "); html += jsonFrases; html += F(";");

  html += F("function renderFrases() {");
  html += F("  var groups=[{k:'sarcastica',l:'Sarcastica'},{k:'sensivel',l:'Sensivel'},{k:'normal',l:'Normal'}];");
  html += F("  var humors=[{k:'doente',l:'Doente/Faminto'},{k:'entediado',l:'Entediado'},{k:'feliz',l:'Feliz'}];");
  html += F("  groups.forEach(function(g){");
  html += F("    humors.forEach(function(h){");
  html += F("      var el=document.getElementById('fl_'+g.k+'_'+h.k); if(!el) return; el.innerHTML='';");
  html += F("      if(!frasesData[g.k]) frasesData[g.k]={};");
  html += F("      if(!frasesData[g.k][h.k]) frasesData[g.k][h.k]=[];");
  html += F("      frasesData[g.k][h.k].forEach(function(frase,idx){");
  html += F("        var li=document.createElement('div');");
  html += F("        li.style.cssText='display:flex;align-items:center;gap:6px;margin-bottom:5px;';");
  html += F("        var inp=document.createElement('input'); inp.type='text'; inp.value=frase;");
  html += F("        inp.style.cssText='flex:1;background:#1e1e1e;color:#fff;border:1px solid #444;border-radius:5px;padding:6px 8px;font-size:0.9em;font-family:monospace;';");
  html += F("        inp.oninput=(function(gk,hk,i){return function(){frasesData[gk][hk][i]=this.value;};})(g.k,h.k,idx);");
  html += F("        var del=document.createElement('button'); del.innerHTML='✕'; del.title='Remover';");
  html += F("        del.style.cssText='background:#c0392b;color:#fff;border:none;border-radius:4px;padding:5px 9px;cursor:pointer;font-weight:bold;';");
  html += F("        del.onclick=(function(gk,hk,i){return function(){frasesData[gk][hk].splice(i,1);renderFrases();};})(g.k,h.k,idx);");
  html += F("        li.appendChild(inp); li.appendChild(del); el.appendChild(li);");
  html += F("      });");
  html += F("    });");
  html += F("  });");
  html += F("}");
  html += F("function addFrase(gk,hk){");
  html += F("  if(!frasesData[gk]) frasesData[gk]={};");
  html += F("  if(!frasesData[gk][hk]) frasesData[gk][hk]=[];");
  html += F("  frasesData[gk][hk].push('NOVA FRASE'); renderFrases();");
  html += F("}");
  html += F("function exportFrases(){");
  html += F("  var b=new Blob([JSON.stringify(frasesData,null,2)],{type:'application/json'});");
  html += F("  var a=document.createElement('a'); a.href=URL.createObjectURL(b); a.download='frases_robo.json'; a.click();");
  html += F("}");
  html += F("function uploadFrasesFile(){");
  html += F("  var f=document.getElementById('frasesFile').files[0]; if(!f) return;");
  html += F("  var r=new FileReader(); r.onload=function(e){");
  html += F("    try{frasesData=JSON.parse(e.target.result); renderFrases(); alert('JSON carregado! Clique em Salvar no Robo.');}");
  html += F("    catch(err){alert('JSON invalido!');}");
  html += F("  }; r.readAsText(f);");
  html += F("}");
  html += F("function saveFrases(){");
  html += F("  var data=new URLSearchParams(); data.append('json',JSON.stringify(frasesData));");
  html += F("  fetch('/uploadFrases',{method:'POST',body:data}).then(r=>{");
  html += F("    if(r.ok) alert('Frases salvas com sucesso!'); else alert('Erro ao salvar.');");
  html += F("  });");
  html += F("}");

  html += F("function action(url) {");
  html += F("  fetch(url, {method: 'POST'}).then(() => { setTimeout(() => location.reload(), 800); });");
  html += F("}");
  html += F("function saveName() {");
  html += F("  var nameInput = document.getElementById('petName').value;");
  html += F("  fetch('/setName?name=' + encodeURIComponent(nameInput), {method: 'POST'}).then(() => { location.reload(); });");
  html += F("}");
  html += F("function openTab(tabId, btn) {");
  html += F("  var contents = document.getElementsByClassName('tab-content');");
  html += F("  for (var i = 0; i < contents.length; i++) contents[i].classList.remove('active');");
  html += F("  var buttons = document.getElementsByClassName('tab-btn');");
  html += F("  for (var i = 0; i < buttons.length; i++) buttons[i].classList.remove('active');");
  html += F("  document.getElementById(tabId).classList.add('active');");
  html += F("  btn.classList.add('active');");
  html += F("}");
  html += F("function saveConfig() {");
  html += F("  var data = new URLSearchParams();");
  html += F("  data.append('limPlay', document.getElementById('limPlay').value);");
  html += F("  data.append('limSusto', document.getElementById('limSusto').value);");
  html += F("  data.append('limLuzB', document.getElementById('limLuzB').value);");
  html += F("  data.append('limLuzA', document.getElementById('limLuzA').value);");
  html += F("  data.append('limTempB', document.getElementById('limTempB').value);");
  html += F("  data.append('limTempA', document.getElementById('limTempA').value);");
  html += F("  data.append('timeInc', document.getElementById('timeInc').value);");
  html += F("  data.append('petAge', document.getElementById('petAge').value);");
  html += F("  data.append('modoSentinela', document.getElementById('modoSentinela').checked ? '1' : '0');");
  html += F("  data.append('modoPiano', document.getElementById('modoPiano').checked ? '1' : '0');");
  html += F("  data.append('sonoAuto', document.getElementById('sonoAuto').checked ? '1' : '0');");
  html += F("  data.append('silencioso', document.getElementById('modoSilencioso').checked ? '1' : '0');");
  html += F("  data.append('petFome', document.getElementById('petFome').value);");
  html += F("  data.append('petDiversao', document.getElementById('petDiversao').value);");
  html += F("  data.append('petSaude', document.getElementById('petSaude').value);");
  html += F("  data.append('petVinculo', document.getElementById('petVinculo').value);");
  html += F("  data.append('petPers', document.getElementById('petPers').value);");
  html += F("  fetch('/saveConfig', { method: 'POST', body: data }).then(res => {");
  html += F("    if (res.ok) {");
  html += F("      alert('Configuracoes salvas com sucesso!');");
  html += F("      location.reload();");
  html += F("    } else {");
  html += F("      alert('Erro ao salvar configuracoes.');");
  html += F("    }");
  html += F("  });");
  html += F("}");
  html += F("function uploadSkills() {");
  html += F("  var file = document.getElementById('skillsFile').files[0];");
  html += F("  if (!file) return;");
  html += F("  var reader = new FileReader();");
  html += F("  reader.onload = function(e) {");
  html += F("    var content = e.target.result;");
  html += F("    try {");
  html += F("      JSON.parse(content);");
  html += F("      var data = new URLSearchParams();");
  html += F("      data.append('json', content);");
  html += F("      fetch('/uploadSkills', { method: 'POST', body: data }).then(res => {");
  html += F("        if (res.ok) {");
  html += F("          alert('Habilidades carregadas com sucesso!');");
  html += F("          location.reload();");
  html += F("        } else {");
  html += F("          alert('Erro ao enviar o JSON ao ESP32.');");
  html += F("        }");
  html += F("      });");
  html += F("    } catch(err) {");
  html += F("      alert('Arquivo JSON invalido! Verifique a sintaxe.');");
  html += F("    }");
  html += F("  };");
  html += F("  reader.readAsText(file);");
  html += F("}");
  html += F("function triggerSkill(day) {");
  html += F("  fetch('/triggerSkill?day=' + day, { method: 'POST' });");
  html += F("}");
  html += F("function exportSkills() {");
  html += F("  var jsonString = JSON.stringify(skillsData, null, 2);");
  html += F("  var blob = new Blob([jsonString], { type: 'application/json' });");
  html += F("  var url = URL.createObjectURL(blob);");
  html += F("  var a = document.createElement('a');");
  html += F("  a.href = url;");
  html += F("  a.download = 'habilidades_robo.json';");
  html += F("  a.click();");
  html += F("  URL.revokeObjectURL(url);");
  html += F("}");
  html += F("function renderSkills() {");
  html += F("  var list = document.getElementById('skillsList');");
  html += F("  if (!list || !skillsData) return;");
  html += F("  list.innerHTML = '';");
  html += F("  skillsData.sort((a, b) => a.day - b.day);");
  html += F("  skillsData.forEach(skill => {");
  html += F("    var isUnlocked = (idadeDias >= skill.day);");
  html += F("    var item = document.createElement('div');");
  html += F("    item.className = 'skill-item' + (isUnlocked ? ' unlocked' : ' locked');");
  html += F("    var header = document.createElement('div');");
  html += F("    header.className = 'skill-header';");
  html += F("    var title = document.createElement('span');");
  html += F("    title.className = 'skill-title';");
  html += F("    title.innerHTML = (isUnlocked ? '🔓 ' : '🔒 ') + skill.name;");
  html += F("    header.appendChild(title);");
  html += F("    if (isUnlocked) {");
  html += F("      var testBtn = document.createElement('button');");
  html += F("      testBtn.className = 'btn-test-skill';");
  html += F("      testBtn.innerHTML = 'Testar ⚡';");
  html += F("      testBtn.onclick = function() { triggerSkill(skill.day); };");
  html += F("      header.appendChild(testBtn);");
  html += F("    }");
  html += F("    var badge = document.createElement('span');");
  html += F("    badge.className = 'skill-badge';");
  html += F("    badge.innerHTML = 'Dia ' + skill.day;");
  html += F("    header.appendChild(badge);");
  html += F("    var desc = document.createElement('div');");
  html += F("    desc.className = 'skill-desc';");
  html += F("    desc.innerHTML = skill.desc;");
  html += F("    var status = document.createElement('div');");
  html += F("    status.className = 'skill-status';");
  html += F("    status.innerHTML = isUnlocked ? 'Desbloqueada e Ativa' : 'Bloqueada (Requer Dia ' + skill.day + ')';");
  html += F("    item.appendChild(header);");
  html += F("    item.appendChild(desc);");
  html += F("    item.appendChild(status);");
  html += F("    list.appendChild(item);");
  html += F("  });");
  html += F("}");
  html += F("function triggerFace(face) {");
  html += F("  var data = new URLSearchParams();");
  html += F("  data.append('face', face);");
  html += F("  fetch('/triggerFace', { method: 'POST', body: data });");
  html += F("}");
  html += F("window.onload = function() { renderSkills(); renderFrases(); };");
  html += F("</script></head><body>");
  html += F("<div class='card'>");
  
  html += F("<h1>");
  html += nomeRobo;
  html += F("</h1>");
  html += F("<p class='subtitle'>Painel de Controle e Status do Pet</p>");
  
  // Abas de navegação
  html += F("<div class='tabs'>");
  html += F("  <button class='tab-btn active' onclick=\"openTab('tab-status', this)\">Pet Status</button>");
  html += F("  <button class='tab-btn' onclick=\"openTab('tab-skills', this)\">Habilidades</button>");
  html += F("  <button class='tab-btn' onclick=\"openTab('tab-frases', this)\">Frases</button>");
  html += F("  <button class='tab-btn' onclick=\"openTab('tab-config', this)\">Configuracoes</button>");
  html += F("</div>");

  // Conteúdo Aba Status
  html += F("<div id='tab-status' class='tab-content active'>");
  
  // Campo para alterar o nome
  html += F("<div class='name-container'>");
  html += F("<input type='text' id='petName' class='input-name' placeholder='Nome do Pet' value='");
  html += nomeRobo;
  html += F("'>");
  html += F("<button class='btn-save' onclick='saveName()'>Salvar</button>");
  html += F("</div>");
  
  html += F("<div class='stat-row'><span class='stat-label'>Fome</span><div class='progress-bg'><div class='progress-bar bar-fome' style='width: ");
  html += String(fome);
  html += F("%'></div></div><span>");
  html += String(fome);
  html += F("%</span></div>");
  
  html += F("<div class='stat-row'><span class='stat-label'>Diversao</span><div class='progress-bg'><div class='progress-bar bar-brincar' style='width: ");
  html += String(diversao);
  html += F("%'></div></div><span>");
  html += String(diversao);
  html += F("%</span></div>");
  
  html += F("<div class='stat-row'><span class='stat-label'>Saude</span><div class='progress-bg'><div class='progress-bar bar-saude' style='width: ");
  html += String(saude);
  html += F("%'></div></div><span>");
  html += String(saude);
  html += F("%</span></div>");
  
  html += F("<div class='stat-row'><span class='stat-label'>Vinculo</span><div class='progress-bg'><div class='progress-bar bar-vinculo' style='width: ");
  html += String((pontosDeVinculo * 100) / 120);
  html += F("%'></div></div><span>");
  html += String(pontosDeVinculo);
  html += F("/120</span></div>");
  
  html += F("<div class='env-info'>");
  html += F("<span>Temp: "); html += String((int)temperatura); html += F("&deg;C</span>");
  html += F("<span>Umid: "); html += String((int)umidade); html += F("%</span>");
  html += F("<span>Luz: "); html += String(luzPorcento); html += F("%</span>");
  html += F("</div>");

  html += F("<div style='margin-top: 15px; font-size: 0.95em;'>");
  html += F("<strong>Personalidade:</strong> ");
  if (personalidade == PERSONALIDADE_BASICA) html += F("Basica");
  else if (personalidade == PERSONALIDADE_SARCASTICA) html += F("Sarcastica");
  else if (personalidade == PERSONALIDADE_SENSIVEL) html += F("Sensivel");
  
  html += F("<br><strong>Idade:</strong> ");
  html += String(idadeDias);
  html += (idadeDias == 1 ? F(" dia") : F(" dias"));
  
  if (estaDoente) {
    html += F("<br><span style='color: #ff4757; font-weight: bold;'>[DOENTE! PRECISA DE REMEDIO]</span>");
  }
  html += F("</div>");
  
  html += F("<div class='btn-container'>");
  html += F("<button class='btn-feed' onclick=\"action('/feed')\">Alimentar &#127822;</button>");
  html += F("<button class='btn-play' onclick=\"action('/play')\">Brincar &#9978;</button>");
  html += F("<button class='btn-cure' onclick=\"action('/cure')\">Curar &#128138;</button>");
  html += F("</div>");
  
  html += F("</div>"); // fim tab-status

  // Conteúdo Aba Habilidades
  html += F("<div id='tab-skills' class='tab-content'>");
  html += F("  <div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;'>");
  html += F("    <h3 style='margin: 0; color: #bb86fc;'>Árvore de Aprendizados</h3>");
  html += F("    <div style='display: flex; gap: 8px;'>");
  html += F("      <button class='btn-save' style='padding: 6px 12px; font-size: 0.85em; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick='exportSkills()'>Exportar JSON</button>");
  html += F("      <label class='btn-save' style='cursor: pointer; display: inline-block; padding: 6px 12px; background: #2a2a2a; border: 1px solid #444; border-radius: 6px; font-size: 0.85em; font-weight: bold;'>");
  html += F("        Importar JSON");
  html += F("        <input type='file' id='skillsFile' accept='.json' style='display: none;' onchange='uploadSkills()'>");
  html += F("      </label>");
  html += F("    </div>");
  html += F("  </div>");
  html += F("  <div id='skillsList'></div>");
  html += F("</div>"); // fim tab-skills

  // ===== ABA FRASES =====
  html += F("<div id='tab-frases' class='tab-content'>");
  html += F("<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;margin-bottom:16px;'>");
  html += F("  <span style='font-weight:bold;font-size:1.1em;color:#bb86fc;'>💬 Editor de Frases do Robô</span>");
  html += F("  <div style='display:flex;gap:8px;flex-wrap:wrap;'>");
  html += F("    <button class='btn-save' onclick='exportFrases()'>⬇ Exportar JSON</button>");
  html += F("    <label class='btn-save' style='cursor:pointer;margin:0;'>⬆ Importar JSON<input type='file' id='frasesFile' accept='.json' style='display:none' onchange='uploadFrasesFile()'></label>");
  html += F("    <button class='btn-submit' style='padding:8px 16px;' onclick='saveFrases()'>💾 Salvar no Robô</button>");
  html += F("  </div>");
  html += F("</div>");
  html += F("<p style='color:#888;font-size:0.85em;margin-bottom:16px;'>Edite as frases que o robô exibe na bolha de fala (disponível a partir do Dia 15). Organize por personalidade e humor.</p>");

  // Macro para renderizar cada seção de personalidade + humor
  // Sarcástica
  html += F("<div class='config-group'><label style='color:#ff6b81;font-size:1.05em;'>😏 Sarcástica</label>");
  html += F("<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-top:10px;'>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😷 Doente / Faminto</div><div id='fl_sarcastica_doente'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('sarcastica','doente')\">+ Adicionar Frase</button></div>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😒 Entediado</div><div id='fl_sarcastica_entediado'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('sarcastica','entediado')\">+ Adicionar Frase</button></div>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😌 Feliz</div><div id='fl_sarcastica_feliz'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('sarcastica','feliz')\">+ Adicionar Frase</button></div>");
  html += F("</div></div>");

  // Sensível / Carente
  html += F("<div class='config-group'><label style='color:#74b9ff;font-size:1.05em;'>🥺 Sensível / Carente</label>");
  html += F("<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-top:10px;'>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😷 Doente / Faminto</div><div id='fl_sensivel_doente'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('sensivel','doente')\">+ Adicionar Frase</button></div>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😒 Entediado</div><div id='fl_sensivel_entediado'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('sensivel','entediado')\">+ Adicionar Frase</button></div>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😌 Feliz</div><div id='fl_sensivel_feliz'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('sensivel','feliz')\">+ Adicionar Frase</button></div>");
  html += F("</div></div>");

  // Normal
  html += F("<div class='config-group'><label style='color:#2ed573;font-size:1.05em;'>🙂 Normal</label>");
  html += F("<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-top:10px;'>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😷 Doente / Faminto</div><div id='fl_normal_doente'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('normal','doente')\">+ Adicionar Frase</button></div>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😒 Entediado</div><div id='fl_normal_entediado'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('normal','entediado')\">+ Adicionar Frase</button></div>");
  html += F("  <div><div style='color:#aaa;font-size:0.85em;margin-bottom:6px;'>😌 Feliz</div><div id='fl_normal_feliz'></div><button class='btn-save' style='width:100%;margin-top:4px;' onclick=\"addFrase('normal','feliz')\">+ Adicionar Frase</button></div>");
  html += F("</div></div>");

  html += F("</div>"); // fim tab-frases

  // Conteúdo Aba Configurações
  html += F("<div id='tab-config' class='tab-content'>");
  
  html += F("  <div class='config-group'>");
  html += F("    <label>Simular Tempo de Vida</label>");
  html += F("    <div class='config-field'>");
  html += F("      <label>Idade do Pet (dias)</label>");
  html += F("      <input type='number' id='petAge' class='config-input' value='");
  html += String(idadeDias);
  html += F("'>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <div class='config-group'>");
  html += F("    <label>Simular Atributos do Pet (Para Testes)</label>");
  html += F("    <div class='config-row' style='margin-bottom: 10px;'>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Fome (0-100)</label>");
  html += F("        <input type='number' id='petFome' class='config-input' min='0' max='100' value='");
  html += String(fome);
  html += F("'>");
  html += F("      </div>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Diversao (0-100)</label>");
  html += F("        <input type='number' id='petDiversao' class='config-input' min='0' max='100' value='");
  html += String(diversao);
  html += F("'>");
  html += F("      </div>");
  html += F("    </div>");
  html += F("    <div class='config-row' style='margin-bottom: 10px;'>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Saude (0-100)</label>");
  html += F("        <input type='number' id='petSaude' class='config-input' min='0' max='100' value='");
  html += String(saude);
  html += F("'>");
  html += F("      </div>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Vinculo (0-120)</label>");
  html += F("        <input type='number' id='petVinculo' class='config-input' min='0' max='120' value='");
  html += String(pontosDeVinculo);
  html += F("'>");
  html += F("      </div>");
  html += F("    </div>");
  html += F("    <div class='config-field'>");
  html += F("      <label>Personalidade Natural</label>");
  html += F("      <select id='petPers' class='config-input'>");
  html += F("        <option value='0'");
  if (personalidadeNatural == PERSONALIDADE_BASICA) html += F(" selected");
  html += F(">Basica</option>");
  html += F("        <option value='1'");
  if (personalidadeNatural == PERSONALIDADE_SARCASTICA) html += F(" selected");
  html += F(">Sarcastica</option>");
  html += F("        <option value='2'");
  if (personalidadeNatural == PERSONALIDADE_SENSIVEL) html += F(" selected");
  html += F(">Sensivel</option>");
  html += F("      </select>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <div class='config-group'>");
  html += F("    <label>Modos Especiais (Habilidades)</label>");
  html += F("    <div style='display: flex; flex-direction: column; gap: 8px;'>");
  html += F("      <label style='font-size: 0.9em; display: flex; align-items: center; gap: 8px; cursor: pointer;'>");
  html += F("        <input type='checkbox' id='modoSentinela' value='1'");
  if (modoSentinela) html += F(" checked");
  if (idadeDias < 10) html += F(" disabled");
  html += F("> Modo Sentinela (Requer Dia 10)");
  html += F("      </label>");
  html += F("      <label style='font-size: 0.9em; display: flex; align-items: center; gap: 8px; cursor: pointer;'>");
  html += F("        <input type='checkbox' id='modoPiano' value='1'");
  if (modoPiano) html += F(" checked");
  if (idadeDias < 21) html += F(" disabled");
  html += F("> Modo Piano no Botão (Requer Dia 21)");
  html += F("      </label>");
  html += F("      <label style='font-size: 0.9em; display: flex; align-items: center; gap: 8px; cursor: pointer;'>");
  html += F("        <input type='checkbox' id='sonoAuto' value='1'");
  if (sonoAutomaticoHorario) html += F(" checked");
  html += F("> Dormir por Horario (22h - 8h)");
  html += F("      </label>");
  html += F("      <label style='font-size: 0.9em; display: flex; align-items: center; gap: 8px; cursor: pointer; color: #bb86fc;'>");
  html += F("        <input type='checkbox' id='modoSilencioso' value='1'");
  if (modoSilencioso) html += F(" checked");
  html += F("> 🔇 Modo Silencioso (Desativar Sons)");
  html += F("      </label>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <div class='config-group'>");
  html += F("    <label>Sensor de Som (0 a 4095)</label>");
  html += F("    <div class='config-row'>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Limiar de Brincar</label>");
  html += F("        <input type='number' id='limPlay' class='config-input' value='");
  html += String(limiarBrincar);
  html += F("'>");
  html += F("      </div>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Limiar de Susto</label>");
  html += F("        <input type='number' id='limSusto' class='config-input' value='");
  html += String(limiarSusto);
  html += F("'>");
  html += F("      </div>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <div class='config-group'>");
  html += F("    <label>Sensor de Luz (LDR %)</label>");
  html += F("    <div class='config-row'>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Limiar Escuro/Dormir</label>");
  html += F("        <input type='number' id='limLuzB' class='config-input' min='0' max='100' value='");
  html += String(limiarLuzBaixo);
  html += F("'>");
  html += F("      </div>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Limiar Luz Forte</label>");
  html += F("        <input type='number' id='limLuzA' class='config-input' min='0' max='100' value='");
  html += String(limiarLuzAlto);
  html += F("'>");
  html += F("      </div>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <div class='config-group'>");
  html += F("    <label>Sensor de Temperatura (&deg;C)</label>");
  html += F("    <div class='config-row'>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Limiar Frio</label>");
  html += F("        <input type='number' step='0.1' id='limTempB' class='config-input' value='");
  html += String(limiarTempBaixo, 1);
  html += F("'>");
  html += F("      </div>");
  html += F("      <div class='config-field'>");
  html += F("        <label>Limiar Calor</label>");
  html += F("        <input type='number' step='0.1' id='limTempA' class='config-input' value='");
  html += String(limiarTempAlto, 1);
  html += F("'>");
  html += F("      </div>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <div class='config-group'>");
  html += F("    <label>Incubacao do Ovo</label>");
  html += F("    <div class='config-field'>");
  html += F("      <label>Tempo para Chocar (segundos)</label>");
  html += F("      <input type='number' id='timeInc' class='config-input' value='");
  html += String(tempoIncubacaoSegundos);
  html += F("'>");
  html += F("    </div>");
  html += F("  </div>");


  html += F("  <div class='config-group'>");
  html += F("    <label>Testador de Expressões / Carinhas (4 Segundos)</label>");
  html += F("    <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; margin-top: 10px;'>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('normal')\">Normal 🙂</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('irritado')\">Bravo 😠</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('susto')\">Surpreso 😮</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('espirro')\">Espirro 🤧</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('comendo')\">Comendo 😋</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('brincando')\">Brincando 😺</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('curando')\">Curando 🏥</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('acariciado')\">Carinho 🥰</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('tonto')\">Tonto 😵</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('coracao')\">Corações 😍</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('><_baixo')\">&gt;&lt; Triste ☹️</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('--_vvv')\">- - Bravo 😤</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('piscadela')\">Piscadela 😉</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('beijo')\">Beijo 😘</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('choro')\">Chorando 😭</button>");
  html += F("      <button class='btn-save' style='padding: 8px; background: #2a2a2a; border: 1px solid #444; color:#fff; border-radius:6px; cursor:pointer; font-weight:bold;' onclick=\"triggerFace('oculos')\">Óculos 😎</button>");
  html += F("    </div>");
  html += F("  </div>");

  html += F("  <button class='btn-submit' onclick='saveConfig()'>Salvar Configurações</button>");
  html += F("</div>"); // fim tab-config

  html += F("</div></body></html>");
  server.send(200, "text/html", html);
}

void handleFeed() {
  if (estadoNascimento == ESTADO_NASCIDO) {
    alimentarPet();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Ovo nao chocado");
  }
}

void handlePlay() {
  if (estadoNascimento == ESTADO_NASCIDO) {
    brincarPet(true);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Ovo nao chocado");
  }
}

void handleCure() {
  if (estadoNascimento == ESTADO_NASCIDO) {
    curarPet();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Ovo nao chocado");
  }
}

void handleSetName() {
  if (server.hasArg("name")) {
    nomeRobo = server.arg("name");
    if (nomeRobo.length() > 15) {
      nomeRobo = nomeRobo.substring(0, 15);
    }
    salvarEstado();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Nome invalido");
  }
}

void handleSaveConfig() {
  if (server.hasArg("limPlay")) limiarBrincar = server.arg("limPlay").toInt();
  if (server.hasArg("limSusto")) limiarSusto = server.arg("limSusto").toInt();
  if (server.hasArg("limLuzB")) limiarLuzBaixo = server.arg("limLuzB").toInt();
  if (server.hasArg("limLuzA")) limiarLuzAlto = server.arg("limLuzA").toInt();
  if (server.hasArg("limTempB")) limiarTempBaixo = server.arg("limTempB").toFloat();
  if (server.hasArg("limTempA")) limiarTempAlto = server.arg("limTempA").toFloat();
  if (server.hasArg("timeInc")) tempoIncubacaoSegundos = server.arg("timeInc").toInt();
  
  if (server.hasArg("petAge")) {
    idadeDias = server.arg("petAge").toInt();
    segundosDeVida = (unsigned long)idadeDias * 86400;
  }
  if (server.hasArg("modoSentinela")) {
    modoSentinela = (server.arg("modoSentinela") == "1");
  }
  if (server.hasArg("modoPiano")) {
    modoPiano = (server.arg("modoPiano") == "1");
  }
  if (server.hasArg("sonoAuto")) {
    sonoAutomaticoHorario = (server.arg("sonoAuto") == "1");
  }
  if (server.hasArg("silencioso")) {
    bool antigoSilencioso = modoSilencioso;
    modoSilencioso = (server.arg("silencioso") == "1");
    if (antigoSilencioso != modoSilencioso) {
      feedbackSilenciosoMsg = modoSilencioso ? "MODO SILENCIOSO" : "SOM ATIVADO";
      tempoFimFeedbackSilencioso = millis() + 2000;
      if (!modoSilencioso) {
        myTone(880, 150);
      }
    }
  }
  
  if (server.hasArg("petFome")) fome = server.arg("petFome").toInt();
  if (server.hasArg("petDiversao")) diversao = server.arg("petDiversao").toInt();
  if (server.hasArg("petSaude")) saude = server.arg("petSaude").toInt();
  if (server.hasArg("petVinculo")) pontosDeVinculo = server.arg("petVinculo").toInt();
  if (server.hasArg("petPers")) {
    personalidadeNatural = (Personalidade)server.arg("petPers").toInt();
    personalidade = personalidadeNatural;
  }
  
  salvarEstado();
  server.send(200, "text/plain", "OK");
}

void handleUploadSkills() {
  if (server.hasArg("json")) {
    jsonSkills = server.arg("json");
    preferences.begin("tamagotchi", false);
    preferences.putString("jsonSkills", jsonSkills);
    preferences.end();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "JSON ausente");
  }
}

void handleUploadFrases() {
  if (server.hasArg("json")) {
    jsonFrases = server.arg("json");
    preferences.begin("tamagotchi", false);
    preferences.putString("jsonFrases", jsonFrases);
    preferences.end();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "JSON ausente");
  }
}

void handleGetFrases() {
  server.send(200, "application/json", jsonFrases);
}

void handleTriggerSkill() {
  if (server.hasArg("day")) {
    int day = server.arg("day").toInt();
    if (idadeDias >= day) {
      mudarTelaPara(TELA_FACE);
      
      if (day == 1) {
        tipoReacaoOciosa = 1;
        tempoFimReacaoOciosa = millis() + 2500;
        playRTTTL(rtttl_mario);
      }
      else if (day == 2) {
        animacaoHighFive = true;
        tempoInicioHighFive = millis();
        myTone(880, 100); delay(150); myTone(1047, 200);
      }
      else if (day == 3) {
        animacaoDance = true;
        tempoInicioDance = millis();
        playRTTTL("party:d=16,o=6,b=200:c,e,g,c,e,g");
      }
      else if (day == 5) {
        animacaoDado = true;
        tempoInicioDado = millis();
        valorDado = random(1, 7);
        myTone(150, 50); delay(70); myTone(250, 50);
      }
      else if (day == 7) {
        tipoReacaoOciosa = 8;
        tempoFimReacaoOciosa = millis() + 2000;
      }
      else if (day == 10) {
        myTone(2000, 300);
      }
      else if (day == 14) {
        petTagPresente = true;
      }
      else if (day == 15) {
        tempoProximaFala = millis();
        tempoFimFala = millis() + 4000;
        textoFala = "TESTANDO FALA!";
      }
      else if (day == 21) {
        myTone(523, 100); delay(120);
        myTone(659, 100); delay(120);
        myTone(784, 200);
      }
      
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Bloqueada");
    }
  } else {
    server.send(400, "text/plain", "Falta dia");
  }
}

void handleTriggerFace() {
  if (server.hasArg("face")) {
    String face = server.arg("face");
    mudarTelaPara(TELA_FACE);
    
    animacaoSusto = false;
    animacaoIrritado = false;
    animacaoEspirro = false;
    animacaoComendo = false;
    animacaoBrincando = false;
    animacaoCurando = false;
    animacaoAcariciado = false;
    tipoReacaoOciosa = 0;
    forcarChoro = false;
    forcarOculos = false;
    variacaoMovimento = 0;
    
    tempoFimTesteFace = millis() + 4000;
    
    if (face == "irritado") {
      animacaoIrritado = true;
      tempoInicioIrritado = millis();
    }
    else if (face == "susto") {
      animacaoSusto = true;
      tempoInicioSusto = millis();
    }
    else if (face == "espirro") {
      animacaoEspirro = true;
      tempoInicioEspirro = millis();
    }
    else if (face == "comendo") {
      animacaoComendo = true;
      tempoInicioReacao = millis();
    }
    else if (face == "brincando") {
      animacaoBrincando = true;
      tempoInicioReacao = millis();
    }
    else if (face == "curando") {
      animacaoCurando = true;
      tempoInicioReacao = millis();
    }
    else if (face == "acariciado") {
      animacaoAcariciado = true;
      tempoInicioReacao = millis();
    }
    else if (face == "tonto") {
      variacaoMovimento = 10000;
      tempoUltimoChoque = millis() + 2000;
    }
    else if (face == "coracao") {
      tipoReacaoOciosa = 12;
      tempoFimReacaoOciosa = millis() + 4000;
    }
    else if (face == "><_baixo") {
      tipoReacaoOciosa = 8;
      tempoFimReacaoOciosa = millis() + 4000;
    }
    else if (face == "--_vvv") {
      tipoReacaoOciosa = 9;
      tempoFimReacaoOciosa = millis() + 4000;
    }
    else if (face == "piscadela") {
      tipoReacaoOciosa = 10;
      tempoFimReacaoOciosa = millis() + 4000;
    }
    else if (face == "beijo") {
      tipoReacaoOciosa = 14;
      tempoFimReacaoOciosa = millis() + 4000;
    }
    else if (face == "choro") {
      forcarChoro = true;
      tempoFimForcarChoro = millis() + 4000;
    }
    else if (face == "oculos") {
      forcarOculos = true;
      tempoFimForcarOculos = millis() + 4000;
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Falta face");
  }
}

void inicializarWiFi() {
  Serial.print(F("Conectando ao WiFi: "));
  Serial.println(ssid);
  
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(10, 20, "CONECTANDO WIFI...");
    u8g2.drawStr(10, 42, ssid);
  } while (u8g2.nextPage());
  
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    wifiConectado = true;
    
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(10, 20, "WIFI CONECTADO!");
      u8g2.drawStr(10, 38, "IP DO ROBO:");
      u8g2.setFont(u8g2_font_profont12_tf);
      u8g2.drawStr(10, 54, WiFi.localIP().toString().c_str());
    } while (u8g2.nextPage());
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(3000);
  } else {
    Serial.println("\nFalha na conexao WiFi. Continuando Offline.");
    wifiConectado = false;
    u8g2.firstPage();
    do {
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(10, 25, "CONEXAO FALHOU!");
      u8g2.drawStr(10, 45, "MODO OFFLINE...");
    } while (u8g2.nextPage());
    delay(2000);
  }
}

// --- ESP-NOW CONFIGURAÇÃO E ENVIO ---
uint8_t macCabeca[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Endereço de Broadcast

struct __attribute__((packed)) RobotState {
  uint8_t fome;
  uint8_t diversao;
  uint8_t saude;
  uint8_t humor;           // 0=FELIZ, 1=ENTEDIADO, 2=CARENTE, 3=DOENTE
  uint8_t estadoNascimento; // 0=OVO, 1=CHOCANDO, 2=NASCIDO
  uint8_t personalidade;   // 0=BASICA, 1=SARCASTICA, 2=SENSIVEL
  uint32_t idadeDias;
  float temperatura;
  float umidade;
  uint8_t luzPorcento;
  bool petDormindo;
  bool choqueDetectado;
  bool obstaculoDetectado;
  bool animacaoComendo;
  bool animacaoBrincando;
  bool animacaoCurando;
  bool animacaoAcariciado;
  bool animacaoHighFive;
  bool animacaoDado;
  bool animacaoDance;
  uint8_t valorDado;
  char fala[64];
  
  // Novos campos para a Fase 2 (RFID integrado)
  bool rfidLido;
  uint8_t rfidUID[4];
  
  // Novos campos para a arquitetura Cérebro-Corpo
  uint8_t rfidAcao;
  uint16_t somNivel;
  bool botaoPressionado;
  
  // Limiares configuráveis da página web
  uint16_t limiarBrincar;
  uint16_t limiarSusto;
  uint8_t limiarLuzBaixo;
  uint8_t limiarLuzAlto;
  float limiarTempBaixo;
  float limiarTempAlto;
};

RobotState estadoParaEnvio;
unsigned long ultimoEnvioEspNow = 0;

void inicializarESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }
  
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, macCabeca, 6);
  peerInfo.channel = 0; // 0 define canal dinâmico correspondente ao da rede
  peerInfo.ifidx = WIFI_IF_STA; // Força o uso da interface Station do Wi-Fi
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Falha ao adicionar peer de Broadcast");
    return;
  }
  Serial.println("ESP-NOW de Broadcast Inicializado com Sucesso!");
}

void enviarEstadoPorEspNow() {
  estadoParaEnvio.fome = fome;
  estadoParaEnvio.diversao = diversao;
  estadoParaEnvio.saude = saude;
  estadoParaEnvio.humor = (uint8_t)humorAtual;
  estadoParaEnvio.estadoNascimento = (uint8_t)estadoNascimento;
  estadoParaEnvio.personalidade = (uint8_t)personalidade;
  estadoParaEnvio.idadeDias = idadeDias;
  estadoParaEnvio.temperatura = temperatura;
  estadoParaEnvio.umidade = umidade;
  estadoParaEnvio.luzPorcento = (uint8_t)luzPorcento;
  estadoParaEnvio.petDormindo = petDormindo;
  estadoParaEnvio.choqueDetectado = choqueDetectado;
  estadoParaEnvio.obstaculoDetectado = (digitalRead(obstaculoPin) == LOW);
  
  estadoParaEnvio.animacaoComendo = animacaoComendo;
  estadoParaEnvio.animacaoBrincando = animacaoBrincando;
  estadoParaEnvio.animacaoCurando = animacaoCurando;
  estadoParaEnvio.animacaoAcariciado = animacaoAcariciado;
  estadoParaEnvio.animacaoHighFive = animacaoHighFive;
  estadoParaEnvio.animacaoDado = animacaoDado;
  estadoParaEnvio.animacaoDance = animacaoDance;
  estadoParaEnvio.valorDado = (uint8_t)valorDado;
  
  if (tempoFimFala > 0 && millis() < tempoFimFala) {
    strncpy(estadoParaEnvio.fala, textoFala.c_str(), sizeof(estadoParaEnvio.fala));
    estadoParaEnvio.fala[sizeof(estadoParaEnvio.fala) - 1] = '\0';
  } else {
    estadoParaEnvio.fala[0] = '\0';
  }
  
  // Copia os dados de RFID pendentes para transmissão e consome o evento
  estadoParaEnvio.rfidLido = rfidLidoPendente;
  memcpy(estadoParaEnvio.rfidUID, rfidUIDPendente, 4);
  rfidLidoPendente = false;
  memset(rfidUIDPendente, 0, 4);
  
  // Novos campos para a arquitetura Cérebro-Corpo
  estadoParaEnvio.rfidAcao = rfidAcaoPendente;
  estadoParaEnvio.somNivel = somMaxDevGlobal;
  estadoParaEnvio.botaoPressionado = (digitalRead(botaoPin) == LOW);
  
  // Limiares configuráveis definidos via Web
  estadoParaEnvio.limiarBrincar = (uint16_t)limiarBrincar;
  estadoParaEnvio.limiarSusto = (uint16_t)limiarSusto;
  estadoParaEnvio.limiarLuzBaixo = (uint8_t)limiarLuzBaixo;
  estadoParaEnvio.limiarLuzAlto = (uint8_t)limiarLuzAlto;
  estadoParaEnvio.limiarTempBaixo = limiarTempBaixo;
  estadoParaEnvio.limiarTempAlto = limiarTempAlto;
  
  // Consome o evento de ação RFID
  rfidAcaoPendente = 0;
  
  esp_now_send(macCabeca, (uint8_t *) &estadoParaEnvio, sizeof(estadoParaEnvio));
}

void setup() {
  Serial.begin(115200); // ESP32 roda melhor em baud rate mais alto (115200)
  
  inicializarParticulas();
  
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  // Inicializa LEDC para o buzzer com controle de volume via duty cycle (ESP32 Core v3.0+)
  ledcAttach(buzzerPin, 2000, 8);
  
  SPI.begin(12, 13, 11, 10); // SCK=12, MISO=13, MOSI=11, SS=10
  rfid.PCD_Init(); // Inicializa o RFID MFRC522
  
  // Imprime informações de diagnóstico do RFID no Monitor Serial
  Serial.print(F("RFID MFRC522: "));
  rfid.PCD_DumpVersionToSerial();
  
  Wire.begin(4, 5); // SDA = GPIO 4, SCL = GPIO 5
  Wire.setClock(400000); // Configura o clock do I2C para 400kHz
  
  dht.begin();
  u8g2.begin();
  
  pinMode(botaoPin, INPUT_PULLUP); // Botão com resistor interno na diagonal
  pinMode(obstaculoPin, INPUT);
  pinMode(choquePin, INPUT_PULLUP);

  // Inicializa o WiFi e configura o Web Server
  inicializarWiFi();
  inicializarESPNow();
  if (wifiConectado) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/feed", HTTP_POST, handleFeed);
    server.on("/play", HTTP_POST, handlePlay);
    server.on("/cure", HTTP_POST, handleCure);
    server.on("/setName", HTTP_POST, handleSetName);
    server.on("/saveConfig", HTTP_POST, handleSaveConfig);
    server.on("/uploadSkills", HTTP_POST, handleUploadSkills);
    server.on("/triggerSkill", HTTP_POST, handleTriggerSkill);
    server.on("/triggerFace", HTTP_POST, handleTriggerFace);
    server.on("/uploadFrases", HTTP_POST, handleUploadFrases);
    server.on("/getFrases", HTTP_GET, handleGetFrases);
    server.begin();
    Serial.println("Servidor Web HTTP iniciado!");
  }

  // Se segurar o botão pressionado ao ligar o robô, limpa as preferências (Factory Reset)
  if (digitalRead(botaoPin) == LOW) {
    Serial.println(F("Limpando a particao NVS para reiniciar de fabrica..."));
    preferences.begin("tamagotchi", false);
    preferences.clear();
    preferences.end();
    // Toca som de aviso de reset: bipe longo
    myTone(440, 500);
    delay(600);
  }
  
  // Carrega estado salvo do pet e UIDs do RFID (se houver)
  carregarEstado();
}

// --- FUNÇÕES DE RENDERIZAÇÃO DAS TELAS COM SUPORTE A SLIDE ---
void desenharTelaTemp(int xOffset) {
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.drawStr(0 + xOffset, 12, "TEMPERATURA");
  u8g2.drawLine(0 + xOffset, 16, 75 + xOffset, 16);
  
  int tempVal = (int)temperatura;
  draw7SegmentNumber(0 + xOffset, 22, tempVal);
  int w = get7SegmentNumberWidth(tempVal);
  
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.drawCircle(w + 3 + xOffset, 24, 1);
  u8g2.drawStr(w + 7 + xOffset, 32, "C");
  
  if (temperatura > limiarTempAlto) {
    u8g2.drawStr(0 + xOffset, 58, "CALOR");
    u8g2.drawCircle(100 + xOffset, 32, 10);
    u8g2.drawLine(100 + xOffset, 16, 100 + xOffset, 20);
    u8g2.drawLine(100 + xOffset, 44, 100 + xOffset, 48);
    u8g2.drawLine(84 + xOffset, 32, 88 + xOffset, 32);
    u8g2.drawLine(112 + xOffset, 32, 116 + xOffset, 32);
    u8g2.drawLine(89 + xOffset, 21, 92 + xOffset, 24);
    u8g2.drawLine(111 + xOffset, 21, 108 + xOffset, 24);
    u8g2.drawLine(89 + xOffset, 43, 92 + xOffset, 40);
    u8g2.drawLine(111 + xOffset, 43, 108 + xOffset, 40);
  } 
  else if (temperatura < limiarTempBaixo) {
    u8g2.drawStr(0 + xOffset, 58, "FRIO");
    u8g2.drawLine(100 + xOffset, 16, 100 + xOffset, 48);
    u8g2.drawLine(84 + xOffset, 32, 116 + xOffset, 32);
    u8g2.drawLine(89 + xOffset, 21, 111 + xOffset, 43);
    u8g2.drawLine(89 + xOffset, 43, 111 + xOffset, 21);
    u8g2.drawLine(96 + xOffset, 22, 100 + xOffset, 18); u8g2.drawLine(104 + xOffset, 22, 100 + xOffset, 18);
    u8g2.drawLine(96 + xOffset, 42, 100 + xOffset, 46); u8g2.drawLine(104 + xOffset, 42, 100 + xOffset, 46);
    u8g2.drawLine(90 + xOffset, 28, 86 + xOffset, 32); u8g2.drawLine(90 + xOffset, 36, 86 + xOffset, 32);
    u8g2.drawLine(110 + xOffset, 28, 114 + xOffset, 32); u8g2.drawLine(110 + xOffset, 36, 114 + xOffset, 32);
  } 
  else {
    u8g2.drawStr(0 + xOffset, 58, "AGRADAVEL");
    u8g2.drawCircle(100 + xOffset, 44, 6);
    u8g2.drawFrame(98 + xOffset, 16, 5, 24);
    u8g2.drawBox(99 + xOffset, 26, 3, 16);
    u8g2.drawDisc(100 + xOffset, 44, 4);
    u8g2.setDrawColor(0); u8g2.drawLine(99 + xOffset, 40, 101 + xOffset, 40); u8g2.setDrawColor(1);
  }
}

void desenharTelaUmid(int xOffset) {
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.drawStr(0 + xOffset, 12, "UMIDADE");
  u8g2.drawLine(0 + xOffset, 16, 75 + xOffset, 16);
  
  int umidVal = (int)umidade;
  draw7SegmentNumber(0 + xOffset, 22, umidVal);
  int w = get7SegmentNumberWidth(umidVal);
  
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.drawStr(w + 3 + xOffset, 36, "%");
  
  if (umidade > 75) {
    u8g2.drawStr(0 + xOffset, 58, "ALTA UMID");
    u8g2.drawDisc(100 + xOffset, 36, 10);
    u8g2.drawTriangle(100 + xOffset, 16, 91 + xOffset, 32, 109 + xOffset, 32);
    u8g2.setDrawColor(0); u8g2.drawDisc(96 + xOffset, 32, 2); u8g2.setDrawColor(1);
  } 
  else if (umidade < 35) {
    u8g2.drawStr(0 + xOffset, 58, "AR SECO");
    u8g2.drawRBox(97 + xOffset, 18, 6, 30, 2); 
    u8g2.drawBox(91 + xOffset, 28, 7, 4); 
    u8g2.drawRBox(91 + xOffset, 20, 4, 10, 1); 
    u8g2.drawBox(102 + xOffset, 32, 7, 4); 
    u8g2.drawRBox(105 + xOffset, 22, 4, 12, 1); 
    u8g2.drawBox(92 + xOffset, 48, 16, 6); 
  } 
  else {
    u8g2.drawStr(0 + xOffset, 58, "AGRADAVEL");
    u8g2.drawDisc(92 + xOffset, 38, 8);
    u8g2.drawDisc(100 + xOffset, 32, 10);
    u8g2.drawDisc(109 + xOffset, 38, 7);
    u8g2.drawBox(84 + xOffset, 38, 32, 8);
  }
}

void desenharTelaRelogio(int xOffset) {
  u8g2.setFont(u8g2_font_profont11_tr);
  String titulo = "RELOGIO DE " + nomeRobo;
  titulo.toUpperCase();
  u8g2.drawStr(64 - u8g2.getStrWidth(titulo.c_str()) / 2 + xOffset, 11, titulo.c_str());
  u8g2.drawLine(0 + xOffset, 13, 128 + xOffset, 13);
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[10];
    char dateStr[12];
    
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    strftime(dateStr, sizeof(dateStr), "%d/%m/%Y", &timeinfo);
    
    u8g2.setFont(u8g2_font_profont15_tf);
    u8g2.drawStr(28 + xOffset, 36, timeStr);
    
    u8g2.setFont(u8g2_font_profont12_tf);
    u8g2.drawStr(24 + xOffset, 52, dateStr);
  } else {
    u8g2.setFont(u8g2_font_profont12_tf);
    u8g2.drawStr(12 + xOffset, 38, "Sem Sincronia NTP");
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(20 + xOffset, 52, wifiConectado ? "Conectando..." : "WiFi Desligado");
  }
}

void desenharTelaIdade(int xOffset) {
  u8g2.setFont(u8g2_font_profont11_tr);
  String titulo = "IDADE DE " + nomeRobo;
  titulo.toUpperCase();
  u8g2.drawStr(64 - u8g2.getStrWidth(titulo.c_str()) / 2 + xOffset, 11, titulo.c_str());
  u8g2.drawLine(0 + xOffset, 13, 128 + xOffset, 13);
  
  u8g2.setFont(u8g2_font_profont15_tf);
  String idadeStr = String(idadeDias) + (idadeDias == 1 ? " DIA" : " DIAS");
  u8g2.drawStr(64 - u8g2.getStrWidth(idadeStr.c_str()) / 2 + xOffset, 36, idadeStr.c_str());
  
  u8g2.setFont(u8g2_font_profont11_tr);
  String totalStr = "TEMPO ATIVO: " + String(segundosDeVida / 3600) + "H";
  u8g2.drawStr(64 - u8g2.getStrWidth(totalStr.c_str()) / 2 + xOffset, 52, totalStr.c_str());
}


void desenharTelaStatus(int xOffset) {
  u8g2.setFont(u8g2_font_profont11_tr);
  String titulo = "STATUS DE " + nomeRobo;
  titulo.toUpperCase();
  u8g2.drawStr(64 - u8g2.getStrWidth(titulo.c_str()) / 2 + xOffset, 11, titulo.c_str());
  u8g2.drawLine(0 + xOffset, 13, 128 + xOffset, 13);
  
  // FOME
  u8g2.drawStr(0 + xOffset, 26, "FOME:");
  u8g2.drawFrame(45 + xOffset, 20, 50, 7);
  u8g2.drawBox(47 + xOffset, 22, fome / 2, 3);
  char fomeStr[8]; itoa(fome, fomeStr, 10);
  u8g2.drawStr(100 + xOffset, 26, fomeStr);
  u8g2.drawStr(120 + xOffset, 26, "%");
  
  // DIVERSÃO (PLAY)
  u8g2.drawStr(0 + xOffset, 42, "BRINCAR:");
  u8g2.drawFrame(45 + xOffset, 36, 50, 7);
  u8g2.drawBox(47 + xOffset, 38, diversao / 2, 3);
  char divStr[8]; itoa(diversao, divStr, 10);
  u8g2.drawStr(100 + xOffset, 42, divStr);
  u8g2.drawStr(120 + xOffset, 42, "%");
  
  // SAÚDE
  u8g2.drawStr(0 + xOffset, 58, "SAUDE:");
  u8g2.drawFrame(45 + xOffset, 50, 50, 7);
  u8g2.drawBox(47 + xOffset, 52, saude / 2, 3);
  char sauStr[8]; itoa(saude, sauStr, 10);
  u8g2.drawStr(100 + xOffset, 58, sauStr);
  u8g2.drawStr(120 + xOffset, 58, "%");
}

void desenharTelaFace(int xOffset) {
  int numIcons = 0;
  int limiarHUD = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
  bool precisaComida = (fome <= limiarHUD);
  bool precisaBrincar = (diversao <= limiarHUD);
  bool precisaSaude = (saude <= limiarHUD || estaDoente);
  if (precisaComida) numIcons++;
  if (precisaBrincar) numIcons++;
  if (precisaSaude) numIcons++;


  if (forcarChoro && millis() < tempoFimForcarChoro) {
    drawEyeSqueezed(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0, true);
    drawEyeSqueezed(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius, 1.0, false);
    u8g2.drawLine(58 + xOffset, 51, 61 + xOffset, 48);
    u8g2.drawLine(61 + xOffset, 48, 67 + xOffset, 48);
    u8g2.drawLine(67 + xOffset, 48, 70 + xOffset, 51);
    int tearY = 32 + ((millis() / 50) % 25);
    u8g2.drawVLine((int)eyeLx - 4 + xOffset, tearY, 4);
    u8g2.drawVLine((int)eyeRx + 4 + xOffset, tearY, 4);
    return;
  }

  if (forcarOculos && millis() < tempoFimForcarOculos) {
    int cx = 64 + xOffset;
    int cy = 48;
    u8g2.drawFilledEllipse(cx, cy, 5, 4);
    u8g2.setDrawColor(0);
    u8g2.drawBox(cx - 6, cy, 12, 4);
    u8g2.setDrawColor(1);
    unsigned long elapsed = 4000 - (tempoFimForcarOculos - millis());
    int glassesY = -12 + (int)(elapsed / 40);
    if (glassesY > (int)eyeLy - 4) glassesY = (int)eyeLy - 4;
    u8g2.drawBox((int)eyeLx - 8 + xOffset, glassesY, 16, 8);
    u8g2.drawBox((int)eyeRx - 8 + xOffset, glassesY, 16, 8);
    u8g2.drawLine((int)eyeLx - 8 + xOffset, glassesY, 30 + xOffset, glassesY + 4);
    u8g2.drawLine((int)eyeRx + 8 + xOffset, glassesY, 98 + xOffset, glassesY + 4);
    u8g2.drawLine((int)eyeLx + 8 + xOffset, glassesY + 2, (int)eyeRx - 8 + xOffset, glassesY + 2);
    return;
  }

  if (animacaoHighFive && idadeDias >= 2) {
    int handX = 64 + xOffset;
    int handY = 32;
    u8g2.drawRFrame(handX - 8, handY - 4, 16, 16, 3);
    u8g2.drawBox(handX - 7, handY - 14, 2, 10); 
    u8g2.drawBox(handX - 4, handY - 17, 2, 13); 
    u8g2.drawBox(handX - 1, handY - 15, 2, 11); 
    u8g2.drawBox(handX + 2, handY - 10, 2, 6);  
    u8g2.drawBox(handX - 12, handY - 1, 4, 2);   
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(64 - u8g2.getStrWidth("HIGH FIVE!") / 2 + xOffset, 58, "HIGH FIVE!");
    return;
  }
  
  if (animacaoDado && idadeDias >= 5) {
    int diceX = 64 + xOffset;
    int diceY = 32;
    u8g2.drawRFrame(diceX - 16, diceY - 16, 32, 32, 4);
    if (millis() - tempoInicioDado < 1500) {
      int rx = diceX - 8 + random(-2, 3);
      int ry = diceY - 8 + random(-2, 3);
      u8g2.drawRFrame(rx, ry, 16, 16, 2);
    } else {
      if (valorDado == 1) {
        u8g2.drawDisc(diceX, diceY, 2);
      } else if (valorDado == 2) {
        u8g2.drawDisc(diceX - 8, diceY - 8, 2);
        u8g2.drawDisc(diceX + 8, diceY + 8, 2);
      } else if (valorDado == 3) {
        u8g2.drawDisc(diceX - 8, diceY - 8, 2);
        u8g2.drawDisc(diceX, diceY, 2);
        u8g2.drawDisc(diceX + 8, diceY + 8, 2);
      } else if (valorDado == 4) {
        u8g2.drawDisc(diceX - 8, diceY - 8, 2);
        u8g2.drawDisc(diceX + 8, diceY - 8, 2);
        u8g2.drawDisc(diceX - 8, diceY + 8, 2);
        u8g2.drawDisc(diceX + 8, diceY + 8, 2);
      } else if (valorDado == 5) {
        u8g2.drawDisc(diceX - 8, diceY - 8, 2);
        u8g2.drawDisc(diceX + 8, diceY - 8, 2);
        u8g2.drawDisc(diceX, diceY, 2);
        u8g2.drawDisc(diceX - 8, diceY + 8, 2);
        u8g2.drawDisc(diceX + 8, diceY + 8, 2);
      } else if (valorDado == 6) {
        u8g2.drawDisc(diceX - 8, diceY - 8, 2);
        u8g2.drawDisc(diceX + 8, diceY - 8, 2);
        u8g2.drawDisc(diceX - 8, diceY, 2);
        u8g2.drawDisc(diceX + 8, diceY, 2);
        u8g2.drawDisc(diceX - 8, diceY + 8, 2);
        u8g2.drawDisc(diceX + 8, diceY + 8, 2);
      }
    }
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(64 - u8g2.getStrWidth("SORTE!") / 2 + xOffset, 58, "SORTE!");
    return;
  }
  
  if (modoSentinela && idadeDias >= 10 && digitalRead(obstaculoPin) == LOW) {
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(64 - u8g2.getStrWidth("ALERTA INVASOR!") / 2 + xOffset, 20, "ALERTA INVASOR!");
    if ((millis() / 250) % 2 == 0) {
      u8g2.drawTriangle(64 + xOffset, 28, 52 + xOffset, 50, 76 + xOffset, 50);
      u8g2.setFont(u8g2_font_profont15_tf);
      u8g2.setDrawColor(0);
      u8g2.drawStr(61 + xOffset, 48, "!");
      u8g2.setDrawColor(1);
    }
    return;
  }

  if (tempoFimFala > 0 && idadeDias >= 15) {
    int bw = u8g2.getStrWidth(textoFala.c_str()) + 8;
    int bx = 64 - bw / 2 + xOffset;
    u8g2.drawRFrame(bx, 2, bw, 13, 3);
    u8g2.drawTriangle(64 - 3 + xOffset, 15, 64 + 3 + xOffset, 15, 64 + xOffset, 18);
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(bx + 4, 11, textoFala.c_str());
  }

  if (animacaoDance && idadeDias >= 3) {
    bool alt = (millis() / 200) % 2;
    if (alt) {
      drawEyeHappy(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0);
      drawEyeSqueezed(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius, 1.0, false);
    } else {
      drawEyeSqueezed(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0, true);
      drawEyeHappy(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius, 1.0);
    }
    int pulseSize = 2 + (millis() / 150) % 3;
    u8g2.drawCircle(64 + xOffset, 48, pulseSize);
    u8g2.setFont(u8g2_font_profont11_tr);
    if ((millis() / 300) % 2 == 0) {
      u8g2.drawStr(24 + xOffset, 20, "*");
      u8g2.drawStr(100 + xOffset, 24, "d"); // note icon or symbol
    } else {
      u8g2.drawStr(22 + xOffset, 24, "d");
      u8g2.drawStr(98 + xOffset, 20, "*");
    }
    u8g2.drawStr(64 - u8g2.getStrWidth("DANCANDO!") / 2 + xOffset, 60, "DANCANDO!");
    return;
  }

  if (animacaoSusto) {
    drawEye(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius);
    drawEye(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius);
    
    u8g2.setDrawColor(0);
    u8g2.drawFilledEllipse((int)eyeLx + xOffset, (int)eyeLy, 4, 4);
    u8g2.drawFilledEllipse((int)eyeRx + xOffset, (int)eyeRy, 4, 4);
    u8g2.setDrawColor(1);

    u8g2.drawCircle(64 + xOffset, 50, 3);
  }
  else if (animacaoEspirro) {
    drawEyeSqueezed(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0, true);
    drawEyeSqueezed(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius, 1.0, false);
    
    u8g2.drawDisc(64 + xOffset, 50, 5);
    
    int tempoSneeze = millis() - tempoInicioEspirro;
    int drop1X = 60 - (tempoSneeze / 20) + xOffset;
    int drop1Y = 52 + (tempoSneeze / 25);
    int drop2X = 64 + xOffset;
    int drop2Y = 54 + (tempoSneeze / 15);
    int drop3X = 68 + (tempoSneeze / 20) + xOffset;
    int drop3Y = 52 + (tempoSneeze / 25);

    if (drop1Y < 64 && drop1X >= 0) u8g2.drawVLine(drop1X, drop1Y, 2);
    if (drop2Y < 64) u8g2.drawVLine(drop2X, drop2Y, 2);
    if (drop3Y < 64 && drop3X < 128) u8g2.drawVLine(drop3X, drop3Y, 2);
  }
  else if (animacaoComendo) {
    drawEyeHappy(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0);
    drawEyeHappy(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius, 1.0);
    
    int mastigando = (millis() / 200) % 2;
    if (mastigando == 0) {
      u8g2.drawDisc(64 + xOffset, 48, 5);
    } else {
      u8g2.drawLine(59 + xOffset, 48, 69 + xOffset, 48);
    }
  }
  else if (animacaoBrincando) {
    drawEyeHappy(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0);
    drawEyeHappy(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius, 1.0);
    
    float avgEyeX = (eyeLx + eyeRx) / 2.0;
    float avgEyeY = (eyeLy + eyeRy) / 2.0;
    float mouthShiftX = (avgEyeX - 64.0) * 0.85;
    float mouthShiftY = (avgEyeY - 32.0) * 0.80;

    u8g2.drawLine((int)(58 + mouthShiftX + xOffset), (int)(47 + mouthShiftY), (int)(61 + mouthShiftX + xOffset), (int)(50 + mouthShiftY));
    u8g2.drawLine((int)(61 + mouthShiftX + xOffset), (int)(50 + mouthShiftY), (int)(64 + mouthShiftX + xOffset), (int)(47 + mouthShiftY));
    u8g2.drawLine((int)(64 + mouthShiftX + xOffset), (int)(47 + mouthShiftY), (int)(67 + mouthShiftX + xOffset), (int)(50 + mouthShiftY));
    u8g2.drawLine((int)(67 + mouthShiftX + xOffset), (int)(50 + mouthShiftY), (int)(70 + mouthShiftX + xOffset), (int)(47 + mouthShiftY));
  }
  else if (animacaoCurando) {
    drawEyeHappy(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius, 1.0);
    u8g2.drawLine((int)eyeRx - 10 + xOffset, (int)eyeRy, (int)eyeRx + 10 + xOffset, (int)eyeRy);
    
    u8g2.drawLine(59 + xOffset, 48, 64 + xOffset, 51);
    u8g2.drawLine(64 + xOffset, 51, 69 + xOffset, 48);
  }
  else if (animacaoAcariciado) {
    u8g2.drawRFrame(44 + xOffset, 30, 40, 26, 8);
    
    u8g2.drawTriangle(44 + xOffset, 30, 52 + xOffset, 30, 40 + xOffset, 18);
    u8g2.drawTriangle(84 + xOffset, 30, 76 + xOffset, 30, 88 + xOffset, 18);
    
    drawEyeHappy(54 + xOffset, 40, 8, 8, 2, 1.0);
    drawEyeHappy(74 + xOffset, 40, 8, 8, 2, 1.0);
    
    u8g2.drawPixel(64 + xOffset, 44);
    u8g2.drawLine(61 + xOffset, 46, 64 + xOffset, 48);
    u8g2.drawLine(64 + xOffset, 48, 67 + xOffset, 46);
    
    u8g2.drawLine(36 + xOffset, 43, 42 + xOffset, 44);
    u8g2.drawLine(35 + xOffset, 47, 42 + xOffset, 46);
    u8g2.drawLine(86 + xOffset, 44, 92 + xOffset, 43);
    u8g2.drawLine(86 + xOffset, 46, 93 + xOffset, 47);
    
    int maoOffset = (int)(sin(millis() * 0.006) * 12.0);
    int maoX = 64 + maoOffset + xOffset;
    int maoY = 16;
    u8g2.drawCircle(maoX, maoY, 6, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawLine(maoX - 6, maoY, maoX - 10, maoY - 2);
  }
  else if (animacaoIrritado) {
    // Carinha de Bravo (Som/Barulho detectado para personalidade Sarcástica)
    drawEye(eyeLx + xOffset, eyeLy, eyeLw, eyeLh, eyeRadius);
    drawEye(eyeRx + xOffset, eyeRy, eyeRw, eyeRh, eyeRadius);
    
    // Sobrancelhas bravas (/ \)
    u8g2.drawLine((int)eyeLx - 10 + xOffset, (int)eyeLy - 12, (int)eyeLx + 8 + xOffset, (int)eyeLy - 8);
    u8g2.drawLine((int)eyeRx - 8 + xOffset, (int)eyeRy - 8, (int)eyeRx + 10 + xOffset, (int)eyeRy - 12);
    
    // Boca linear de irritado/descontentamento
    float avgEyeX = (eyeLx + eyeRx) / 2.0;
    float avgEyeY = (eyeLy + eyeRy) / 2.0;
    float mouthShiftX = (avgEyeX - 64.0) * 0.85;
    float mouthShiftY = (avgEyeY - 32.0) * 0.80;
    u8g2.drawLine((int)(58 + mouthShiftX + xOffset), (int)(49 + mouthShiftY), (int)(70 + mouthShiftX + xOffset), (int)(49 + mouthShiftY));
  }
  else if (variacaoMovimento > 8000) {
    float dx1 = 8.0 * cos(anguloTontura);
    float dy1 = 8.0 * sin(anguloTontura);
    float dx2 = -8.0 * sin(anguloTontura);
    float dy2 = 8.0 * cos(anguloTontura);
    
    u8g2.drawLine((int)eyeLx - dx1 + xOffset, (int)eyeLy - dy1, (int)eyeLx + dx1 + xOffset, (int)eyeLy + dy1);
    u8g2.drawLine((int)eyeLx - dx2 + xOffset, (int)eyeLy - dy2, (int)eyeLx + dx2 + xOffset, (int)eyeLy + dy2);
    
    u8g2.drawLine((int)eyeRx - dx1 + xOffset, (int)eyeRy - dy1, (int)eyeRx + dx1 + xOffset, (int)eyeRy + dy1);
    u8g2.drawLine((int)eyeRx - dx2 + xOffset, (int)eyeRy - dy2, (int)eyeRx + dx2 + xOffset, (int)eyeRy + dy2);
    
    float estrelaAngulo = millis() * 0.005;
    int ex1 = 64 + cos(estrelaAngulo) * 12 + xOffset;
    int ey1 = 10 + sin(estrelaAngulo) * 3;
    int ex2 = 64 + cos(estrelaAngulo + 3.1415) * 12 + xOffset;
    int ey2 = 10 + sin(estrelaAngulo + 3.1415) * 3;
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(ex1 - 2, ey1 + 3, "*");
    u8g2.drawStr(ex2 - 2, ey2 + 3, "*");
  }
  else {
    // --- RENDERIZAÇÃO BASEADA EM HUMOR E ANIMAÇÕES OCIOSAS ---
    int tremorX = 0;
    int tremorY = 0;
    if (tipoReacaoOciosa == 4) { // Tremedeira
      tremorX = random(-1, 2);
      tremorY = random(-1, 2);
    }
    
    // 1. DESENHO DOS OLHOS
    if (tipoReacaoOciosa == 6 || tipoReacaoOciosa == 9) { // Bocejo ou Olhos - - (Sarcástica)
      int shiftY = 0;
      if (tipoReacaoOciosa == 9) {
        shiftY = random(-1, 2); // Jitter vertical de raiva
      }
      u8g2.drawLine((int)eyeLx - 8 + xOffset + tremorX, (int)eyeLy + tremorY + shiftY, (int)eyeLx + 8 + xOffset + tremorX, (int)eyeLy + tremorY + shiftY);
      u8g2.drawLine((int)eyeRx - 8 + xOffset + tremorX, (int)eyeRy + tremorY + shiftY, (int)eyeRx + 8 + xOffset + tremorX, (int)eyeRy + tremorY + shiftY);
    }
    else if (tipoReacaoOciosa == 5 || tipoReacaoOciosa == 8 || tipoReacaoOciosa == 11) { // Espirro ou Olhos >< (Sarcástica/Normal)
      if (personalidade == PERSONALIDADE_SARCASTICA && idadeDias >= 7 && tipoReacaoOciosa == 8) {
        unsigned long elapsed = millis() - (tempoFimReacaoOciosa - 2000);
        int glassesY = -12 + (int)(elapsed / 40);
        if (glassesY > (int)eyeLy - 4) glassesY = (int)eyeLy - 4;
        
        u8g2.drawBox((int)eyeLx - 8 + xOffset + tremorX, glassesY + tremorY, 16, 8);
        u8g2.drawBox((int)eyeRx - 8 + xOffset + tremorX, glassesY + tremorY, 16, 8);
        u8g2.drawLine((int)eyeLx - 8 + xOffset + tremorX, glassesY + tremorY, 30 + xOffset + tremorX, glassesY + tremorY + 4);
        u8g2.drawLine((int)eyeRx + 8 + xOffset + tremorX, glassesY + tremorY, 98 + xOffset + tremorX, glassesY + tremorY + 4);
        u8g2.drawLine((int)eyeLx + 8 + xOffset + tremorX, glassesY + tremorY + 2, (int)eyeRx - 8 + xOffset + tremorX, glassesY + tremorY + 2);
      } else {
        float sqzProgress = 1.0;
        if (tipoReacaoOciosa == 8 || tipoReacaoOciosa == 11) {
          sqzProgress = 0.7 + sin(millis() * 0.012) * 0.3; // Apertando e afrouxando os olhos ><
        }
        drawEyeSqueezed(eyeLx + xOffset + tremorX, eyeLy + tremorY, eyeLw, eyeLh, eyeRadius, sqzProgress, true);
        drawEyeSqueezed(eyeRx + xOffset + tremorX, eyeRy + tremorY, eyeRw, eyeRh, eyeRadius, sqzProgress, false);
      }
    }
    else if (tipoReacaoOciosa == 7) { // Sonho acordado: olhos felizes deformados [EXISTENTE]
      drawEyeHappy(eyeLx + xOffset + tremorX, eyeLy + tremorY, eyeLw, eyeLh, eyeRadius, 1.0);
      drawEyeHappy(eyeRx + xOffset + tremorX, eyeRy + tremorY, eyeRw, eyeRh, eyeRadius, 1.0);
    }
    else if (tipoReacaoOciosa == 12) { // Olhos de coração (Sensível)
      bool heartSmall = (millis() / 400) % 2; // Pulsar/batimento do coração
      drawLargeHeart((int)eyeLx + xOffset + tremorX, (int)eyeLy + tremorY, heartSmall);
      drawLargeHeart((int)eyeRx + xOffset + tremorX, (int)eyeRy + tremorY, heartSmall);
    }
    else if (tipoReacaoOciosa == 10) { // Piscadela única: esquerdo fecha e abre uma vez
      // Olho direito: meia-lua fixa
      drawEyeHappy(eyeRx + xOffset + tremorX, eyeRy + tremorY, eyeRw, eyeRh, eyeRadius, 1.0);
      // Progresso baseado no tempo decorrido desde o início (duração = 2000ms)
      unsigned long winkElapsed = millis() - (tempoFimReacaoOciosa - 2000);
      float winkP; // 0.0 = meia-lua aberta, 1.0 = olho fechado (crescent fino)
      if (winkElapsed < 600) {
        winkP = winkElapsed / 600.0f; // abre -> fecha
      } else if (winkElapsed < 900) {
        winkP = 1.0f; // sustenta fechado
      } else {
        winkP = 1.0f - ((winkElapsed - 900) / 1100.0f); // fecha -> abre
        if (winkP < 0.0f) winkP = 0.0f;
      }
      // Olho esquerdo começa como meia-lua (progress=1.0) e cover cobre o resto
      drawEyeHappy(eyeLx + xOffset + tremorX, eyeLy + tremorY, eyeLw, eyeLh, eyeRadius, 1.0);
      if (winkP > 0.0f) {
        int coverH = (int)(eyeLh * winkP) + 1;
        u8g2.setDrawColor(0);
        u8g2.drawBox((int)(eyeLx - eyeLw/2) + xOffset + tremorX - 1,
                     (int)(eyeLy - eyeLh/2) + tremorY,
                     (int)eyeLw + 2, coverH);
        u8g2.setDrawColor(1);
      }
    }
    else if (tipoReacaoOciosa == 14) { // Beijo: mesma piscada única, boca de beijo
      // Olho direito: meia-lua fixa
      drawEyeHappy(eyeRx + xOffset + tremorX, eyeRy + tremorY, eyeRw, eyeRh, eyeRadius, 1.0);
      // Mesmo progresso de piscada única (duração = 2000ms)
      unsigned long winkElapsed14 = millis() - (tempoFimReacaoOciosa - 2000);
      float winkP14;
      if (winkElapsed14 < 600) {
        winkP14 = winkElapsed14 / 600.0f;
      } else if (winkElapsed14 < 900) {
        winkP14 = 1.0f;
      } else {
        winkP14 = 1.0f - ((winkElapsed14 - 900) / 1100.0f);
        if (winkP14 < 0.0f) winkP14 = 0.0f;
      }
      drawEyeHappy(eyeLx + xOffset + tremorX, eyeLy + tremorY, eyeLw, eyeLh, eyeRadius, 1.0);
      if (winkP14 > 0.0f) {
        int coverH14 = (int)(eyeLh * winkP14) + 1;
        u8g2.setDrawColor(0);
        u8g2.drawBox((int)(eyeLx - eyeLw/2) + xOffset + tremorX - 1,
                     (int)(eyeLy - eyeLh/2) + tremorY,
                     (int)eyeLw + 2, coverH14);
        u8g2.setDrawColor(1);
      }
    }
    else {
      // Olhos normais (com suporte a revirar de olhos)
      drawEye(eyeLx + xOffset + tremorX, eyeLy + tremorY, eyeLw, eyeLh, eyeRadius);
      drawEye(eyeRx + xOffset + tremorX, eyeRy + tremorY, eyeRw, eyeRh, eyeRadius);
      
      if (tipoReacaoOciosa == 2) { // Revirar olhos (Sarcástica)
        float rotAngulo = millis() * 0.01;
        int pupilX = cos(rotAngulo) * 4;
        int pupilY = sin(rotAngulo) * 4;
        u8g2.setDrawColor(0);
        u8g2.drawFilledEllipse((int)eyeLx + xOffset + tremorX + pupilX, (int)eyeLy + tremorY + pupilY, 3, 3);
        u8g2.drawFilledEllipse((int)eyeRx + xOffset + tremorX + pupilX, (int)eyeRy + tremorY + pupilY, 3, 3);
        u8g2.setDrawColor(1);
      }
      
      // Sobrancelhas / pálpebras baseadas na personalidade e humor (oculta o corte no auge da piscada para fechar limpo)
      if ((personalidade == PERSONALIDADE_SARCASTICA || humorAtual == HUMOR_ENTEDIADO) && !estaPiscando) {
        // Corta metade superior dos olhos para cara de tédio
        u8g2.setDrawColor(0);
        u8g2.drawBox((int)(eyeLx - eyeLw/2.0 - 2) + xOffset + tremorX, (int)(eyeLy - eyeLh/2.0 - 1) + tremorY, (int)(eyeLw + 4), (int)(eyeLh * 0.45));
        u8g2.drawBox((int)(eyeRx - eyeRw/2.0 - 2) + xOffset + tremorX, (int)(eyeRy - eyeRh/2.0 - 1) + tremorY, (int)(eyeRw + 4), (int)(eyeRh * 0.45));
        u8g2.setDrawColor(1);
        u8g2.drawLine((int)(eyeLx - eyeLw/2.0 - 1) + xOffset + tremorX, (int)(eyeLy - eyeLh/2.0 + eyeLh * 0.45) + tremorY, (int)(eyeLx + eyeLw/2.0 + 1) + xOffset + tremorX, (int)(eyeLy - eyeLh/2.0 + eyeLh * 0.45) + tremorY);
        u8g2.drawLine((int)(eyeRx - eyeRw/2.0 - 1) + xOffset + tremorX, (int)(eyeRy - eyeRh/2.0 + eyeRh * 0.45) + tremorY, (int)(eyeRx + eyeRw/2.0 + 1) + xOffset + tremorX, (int)(eyeRy - eyeRh/2.0 + eyeRh * 0.45) + tremorY);
      }
      
      // Sobrancelhas de bravo / triste baseadas no humor
      if (humorAtual == HUMOR_CARENTE || humorAtual == HUMOR_DOENTE) {
        // Sobrancelhas tristes (\ /)
        u8g2.drawLine((int)eyeLx - 8 + xOffset + tremorX, (int)eyeLy - 10 + tremorY, (int)eyeLx + 8 + xOffset + tremorX, (int)eyeLy - 14 + tremorY);
        u8g2.drawLine((int)eyeRx - 8 + xOffset + tremorX, (int)eyeRy - 14 + tremorY, (int)eyeRx + 8 + xOffset + tremorX, (int)eyeRy - 10 + tremorY);
      }
    }
    
    // 2. DESENHO DA BOCA
    int offsetMouthY = (int)(sin(millis() * 0.0025) * 0.5);
    float avgEyeX = (eyeLx + eyeRx) / 2.0;
    float avgEyeY = (eyeLy + eyeRy) / 2.0;
    float mouthShiftX = (avgEyeX - 64.0) * 0.85;
    float mouthShiftY = (avgEyeY - 32.0) * 0.80;
    int cx = (int)(64 + mouthShiftX + xOffset + tremorX);
    int cy = (int)(48 + mouthShiftY + tremorY);
    
    if (tipoReacaoOciosa == 6) { // Bocejo: boca aberta grande (O)
      u8g2.drawCircle(cx, cy + 1, 4);
    }
    else if (tipoReacaoOciosa == 5) { // Espirro: boca aberta média
      u8g2.drawDisc(cx, cy + 2, 3);
    }
    else if (tipoReacaoOciosa == 1 || tipoReacaoOciosa == 14) { // Assobio ou Beijo: pequena boca de bico que pulsa
      int pulseSize = 1 + (millis() / 200) % 2;
      u8g2.drawCircle(cx, cy, pulseSize);
    }
    else if (tipoReacaoOciosa == 3) { // Suspiro: boca linear
      u8g2.drawLine(cx - 5, cy + 1, cx + 5, cy + 1);
    }
    else if (tipoReacaoOciosa == 8) { // Boca meia lua preenchida para baixo (Sarcástica)
      u8g2.drawFilledEllipse(cx, cy, 5, 4);
      u8g2.setDrawColor(0);
      u8g2.drawBox(cx - 6, cy, 12, 4); // Corta a parte de baixo (y >= cy) para fazer domo (curvado para baixo)
      u8g2.setDrawColor(1);
    }
    else if (tipoReacaoOciosa == 9) { // Boca VVV (Sarcástica) que treme de irritação
      int rx = cx + random(-1, 2);
      int ry = cy + random(-1, 2);
      u8g2.drawLine(rx - 6, ry - 2, rx - 4, ry + 2);
      u8g2.drawLine(rx - 4, ry + 2, rx - 2, ry - 2);
      u8g2.drawLine(rx - 2, ry - 2, rx, ry + 2);
      u8g2.drawLine(rx, ry + 2, rx + 2, ry - 2);
      u8g2.drawLine(rx + 2, ry - 2, rx + 4, ry + 2);
      u8g2.drawLine(rx + 4, ry + 2, rx + 6, ry - 2);
    }
    else if (tipoReacaoOciosa == 11 || tipoReacaoOciosa == 12) { // Boca meia lua preenchida para cima (Normal/Sensível)
      u8g2.drawFilledEllipse(cx, cy, 5, 4);
      u8g2.setDrawColor(0);
      u8g2.drawBox(cx - 6, cy - 4, 12, 4); // Corta a parte de cima (y <= cy) para fazer bacia (curvado para cima)
      u8g2.setDrawColor(1);
    }
    else if (tipoReacaoOciosa == 10) { // Boca sorridente para piscadela (Normal) que se move
      int mouthOpenY = (millis() / 400) % 2 == 0 ? 3 : 2;
      u8g2.drawLine(cx - 4, cy, cx, cy + mouthOpenY);
      u8g2.drawLine(cx, cy + mouthOpenY, cx + 4, cy);
    }
    else {
      // Boca baseada no humor do pet
      if (humorAtual == HUMOR_DOENTE || humorAtual == HUMOR_CARENTE) {
        // Boca triste curvinha
        u8g2.drawLine((int)(58 + mouthShiftX + xOffset + tremorX), (int)(51 + offsetMouthY + mouthShiftY + tremorY), (int)(61 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY));
        u8g2.drawLine((int)(61 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY), (int)(67 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY));
        u8g2.drawLine((int)(67 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY), (int)(70 + mouthShiftX + xOffset + tremorX), (int)(51 + offsetMouthY + mouthShiftY + tremorY));
      }
      else if (humorAtual == HUMOR_ENTEDIADO) {
        // Boca de desânimo linear
        u8g2.drawLine((int)(59 + mouthShiftX + xOffset + tremorX), (int)(49 + offsetMouthY + mouthShiftY + tremorY), (int)(69 + mouthShiftX + xOffset + tremorX), (int)(49 + offsetMouthY + mouthShiftY + tremorY));
      }
      else {
        // HUMOR_FELIZ: Boca fofa de gatinho (w)
        u8g2.drawLine((int)(60 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY), (int)(62 + mouthShiftX + xOffset + tremorX), (int)(50 + offsetMouthY + mouthShiftY + tremorY));
        u8g2.drawLine((int)(62 + mouthShiftX + xOffset + tremorX), (int)(50 + offsetMouthY + mouthShiftY + tremorY), (int)(64 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY));
        u8g2.drawLine((int)(64 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY), (int)(66 + mouthShiftX + xOffset + tremorX), (int)(50 + offsetMouthY + mouthShiftY + tremorY));
        u8g2.drawLine((int)(66 + mouthShiftX + xOffset + tremorX), (int)(50 + offsetMouthY + mouthShiftY + tremorY), (int)(68 + mouthShiftX + xOffset + tremorX), (int)(48 + offsetMouthY + mouthShiftY + tremorY));
      }
    }
    
    // 3. BALÃO DE PENSAMENTO (HUD)
    if (numIcons > 0) {
      int totalW = (numIcons * 12) + ((numIcons - 1) * 4);
      int startX = 64 - (totalW / 2) + xOffset + tremorX;
      int drawY = 4 + tremorY;
      
      u8g2.drawRFrame(startX - 3, drawY - 2, totalW + 6, 12, 2);
      u8g2.drawPixel(64 + xOffset + tremorX, drawY + 12);
      u8g2.drawPixel(62 + xOffset + tremorX, drawY + 14);

      int currentX = startX;
      if (precisaComida) {
        u8g2.drawBox(currentX + 3, drawY + 3, 6, 2);
        u8g2.drawDisc(currentX + 2, drawY + 2, 1);
        u8g2.drawDisc(currentX + 2, drawY + 5, 1);
        u8g2.drawDisc(currentX + 9, drawY + 2, 1);
        u8g2.drawDisc(currentX + 9, drawY + 5, 1);
        currentX += 16;
      }
      if (precisaBrincar) {
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
        u8g2.drawFrame(currentX + 2, drawY, 8, 7);
        u8g2.drawBox(currentX + 4, drawY - 2, 4, 2);
        u8g2.drawVLine(currentX + 5, drawY + 2, 3);
        u8g2.drawHLine(currentX + 4, drawY + 3, 3);
        currentX += 16;
      }
    }
  }

  if (distancia > 0 && distancia < 10) {
    u8g2.setDrawColor(0);
    u8g2.drawFilledEllipse((int)eyeLx + xOffset, (int)eyeLy, 4, 4);
    u8g2.drawFilledEllipse((int)eyeRx + xOffset, (int)eyeRy, 4, 4);
    u8g2.setDrawColor(1);
  }

  if (temperatura < limiarTempBaixo && temperatura > 0 && !estaDoente && !animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoAcariciado) {
    int tremorMouth = random(-1, 2);
    u8g2.drawLine(54 + xOffset, 48 + tremorMouth, 58 + xOffset, 46 + tremorMouth);
    u8g2.drawLine(58 + xOffset, 46 + tremorMouth, 62 + xOffset, 50 + tremorMouth);
    u8g2.drawLine(62 + xOffset, 50 + tremorMouth, 66 + xOffset, 46 + tremorMouth);
    u8g2.drawLine(66 + xOffset, 46 + tremorMouth, 70 + xOffset, 50 + tremorMouth);
    u8g2.drawLine(70 + xOffset, 50 + tremorMouth, 74 + xOffset, 48 + tremorMouth);
  }

  desenharParticulas(xOffset);

  if (petTagPresente && !animacaoAcariciado) {
    if (petDormindo) {
      u8g2.drawDisc(112 + xOffset, 58, 4);
      u8g2.drawPixel(107 + xOffset, 57);
      u8g2.drawPixel(109 + xOffset, 55);
    } else {
      int cx = (int)catMiniX + xOffset;
      int cy = 54;
      u8g2.setFont(u8g2_font_profont11_tr);
      if (catMiniDirRight) {
        u8g2.drawBox(cx, cy, 8, 5);
        u8g2.drawBox(cx + 6, cy - 2, 4, 4);
        u8g2.drawPixel(cx + 7, cy - 3);
        u8g2.drawPixel(cx + 9, cy - 3);
        u8g2.drawLine(cx, cy, cx - 2, cy - 2);
        if (((millis() / 150) % 2) == 0) {
          u8g2.drawLine(cx + 1, cy + 5, cx + 1, cy + 7);
          u8g2.drawLine(cx + 6, cy + 5, cx + 6, cy + 7);
        } else {
          u8g2.drawLine(cx + 2, cy + 5, cx + 2, cy + 7);
          u8g2.drawLine(cx + 5, cy + 5, cx + 5, cy + 7);
        }
      } else {
        u8g2.drawBox(cx + 2, cy, 8, 5);
        u8g2.drawBox(cx, cy - 2, 4, 4);
        u8g2.drawPixel(cx, cy - 3);
        u8g2.drawPixel(cx + 2, cy - 3);
        u8g2.drawLine(cx + 9, cy, cx + 11, cy - 2);
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

  if (umidade > 80.0 && idadeDias >= 4) {
    int cx = 12 + xOffset;
    int cy = 15;
    u8g2.drawFilledEllipse(cx, cy, 5, 3);
    u8g2.drawFilledEllipse(cx + 4, cy - 2, 4, 3);
    u8g2.drawFilledEllipse(cx - 3, cy + 1, 3, 2);
    u8g2.drawFilledEllipse(cx + 6, cy + 1, 3, 2);
  }

  if (temperatura > 32.0 && idadeDias >= 12) {
    int fx = 116 + xOffset;
    int fy = 15;
    u8g2.drawCircle(fx, fy, 6);
    u8g2.drawCircle(fx, fy, 2);
    float fanAngle = (float)millis() * 0.02f;
    for (int a = 0; a < 3; a++) {
      float rad = fanAngle + (float)a * 2.0943f;
      u8g2.drawLine(fx, fy, fx + (int)(cos(rad) * 5.0), fy + (int)(sin(rad) * 5.0));
    }
  }
}

void desenharTelas(int xOffset, EstadoTela tela) {
  if (tela == TELA_TEMP) desenharTelaTemp(xOffset);
  else if (tela == TELA_UMID) desenharTelaUmid(xOffset);
  else if (tela == TELA_CLOCK) desenharTelaRelogio(xOffset);
  else if (tela == TELA_IDADE) desenharTelaIdade(xOffset);
  else if (tela == TELA_PET_STATUS) desenharTelaStatus(xOffset);
  else if (tela == TELA_FACE) desenharTelaFace(xOffset);
}

void spawnarParticulasOciosas() {
  if (estadoNascimento != ESTADO_NASCIDO) return;
  
  if (animacaoComendo) {
    if (random(0, 100) < 25) {
      criarParticula(64 + random(-6, 7), 48, random(-6, 7) / 10.0, -0.6 - random(0, 5) / 10.0, '*', 30);
    }
  }
  else if (animacaoBrincando) {
    if (random(0, 100) < 18) {
      criarParticula(64 + random(-15, 16), 36, random(-5, 6) / 10.0, -0.5 - random(0, 4) / 10.0, 'H', 35);
    }
  }
  else if (animacaoCurando) {
    if (random(0, 100) < 25) {
      criarParticula(64 + random(-15, 16), 36, random(-5, 6) / 10.0, -0.6 - random(0, 5) / 10.0, '+', 30);
    }
  }
  else if (animacaoAcariciado) {
    if (random(0, 100) < 25) {
      criarParticula(64 + random(-25, 26), 25, random(-8, 9) / 10.0, -0.4 - random(0, 5) / 10.0, 'H', 40);
    }
  }
  else if (petDormindo) {
    if (random(0, 100) < 4) {
      criarParticula(78, 38, 0.15 + random(0, 4) / 10.0, -0.3 - random(0, 3) / 10.0, 'Z', 55);
    }
    if (petTagPresente && random(0, 100) < 3) {
      criarParticula(116, 52, 0.1 + random(0, 3) / 10.0, -0.2 - random(0, 3) / 10.0, 'Z', 45);
    }
  }
  else {
    // Partículas de Micro-Animações Ociosas
    if (tipoReacaoOciosa == 1) { // Assobio
      if (random(0, 100) < 15) {
        criarParticula(64, 48, -0.3 - random(0, 5) / 10.0, -0.5 - random(0, 4) / 10.0, 'M', 35);
      }
    }
    else if (tipoReacaoOciosa == 3) { // Suspiro
      if (random(0, 100) < 12) {
        criarParticula(64, 48, random(-3, 4) / 10.0, -0.4 - random(0, 3) / 10.0, 'P', 25);
      }
    }
    else if (tipoReacaoOciosa == 7) { // Sonho acordado
      if (random(0, 100) < 15) {
        criarParticula(64 + random(-15, 16), 32, random(-5, 6) / 10.0, -0.5 - random(0, 4) / 10.0, 'H', 35);
      }
    }
    else if (tipoReacaoOciosa == 14) { // Beijo: corações flutuando da boca
      if (random(0, 100) < 20) {
        criarParticula(64, 48, 0.4 + random(0, 5) / 10.0, -0.5 - random(0, 4) / 10.0, 'H', 35);
      }
    }

    if (temperatura > limiarTempAlto) {
      if (random(0, 100) < 5) {
        criarParticula(random(5, 123), 0, 0, 0.6 + random(0, 6) / 10.0, 'S', 45);
      }
    }
    int limiarTriste = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
    bool triste = (fome <= limiarTriste || diversao <= limiarTriste || saude <= limiarTriste || estaDoente);
    if (umidade > 75 || (personalidadeNatural == PERSONALIDADE_SENSIVEL && triste)) {
      if (random(0, 100) < 6) {
        criarParticula(eyeLx - 8, eyeLy + 6, -0.15, 0.4 + random(0, 4) / 10.0, 'L', 35);
      }
      if (random(0, 100) < 6) {
        criarParticula(eyeRx + 8, eyeRy + 6, 0.15, 0.4 + random(0, 4) / 10.0, 'L', 35);
      }
    }
  }
}

void calcularHumor() {
  if (estaDoente) {
    humorAtual = HUMOR_DOENTE;
  } else {
    int limiarCarente = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 45 : 30;
    int limiarEntediado = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 55 : 35;
    
    if (fome < limiarCarente || saude < limiarCarente) {
      humorAtual = HUMOR_CARENTE;
    } else if (diversao < limiarEntediado) {
      humorAtual = HUMOR_ENTEDIADO;
    } else {
      humorAtual = HUMOR_FELIZ;
    }
  }
}

void loop() {
  tickRTTTL(); // Avança o player RTTTL não-bloqueante a cada frame
  // Determina a personalidade ativa com base no estado de fome/brincadeira
  int limiarTriste = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
  if (fome <= limiarTriste || (diversao <= limiarTriste && !petTagPresente)) {
    personalidade = PERSONALIDADE_SARCASTICA;
  } else {
    personalidade = personalidadeNatural;
  }

  // --- CONTROLE DE IDADE DO PET (SÓ ENQUANTO ESTIVER NASCIDO) ---
  if (estadoNascimento == ESTADO_NASCIDO) {
    if (tempoUltimoSegundoVida == 0) {
      tempoUltimoSegundoVida = millis();
    }
    if (millis() - tempoUltimoSegundoVida >= 1000) {
      unsigned long elapsed = (millis() - tempoUltimoSegundoVida) / 1000;
      segundosDeVida += elapsed;
      tempoUltimoSegundoVida = millis();
      
      idadeDias = segundosDeVida / 86400; // 1 dia = 86400 segundos
      
      // Salva periodicamente na flash a cada 1 hora (3600000ms) ou quando muda o dia
      static unsigned long ultimoSalvarVida = 0;
      static unsigned int ultimaIdadeSalva = 9999;
      if (millis() - ultimoSalvarVida >= 3600000 || idadeDias != ultimaIdadeSalva) {
        if (ultimoSalvarVida != 0) { // Evita salvar imediatamente ao dar boot
          ultimaIdadeSalva = idadeDias;
          salvarEstado();
        }
        ultimoSalvarVida = millis();
      }
    }
  } else {
    tempoUltimoSegundoVida = 0; // Reseta se ainda não nasceu
  }

  // Falas de Personalidade (Dia 15)
  if (estadoNascimento == ESTADO_NASCIDO && idadeDias >= 15) {
    if (tempoFimFala > 0 && millis() > tempoFimFala) {
      tempoFimFala = 0; // Termina a fala
      tempoProximaFala = millis() + random(15000, 30000); // Agenda próxima fala para 15 a 30s
    }
    if (tempoFimFala == 0 && millis() > tempoProximaFala && !petDormindo && 
        !animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoAcariciado && !animacaoHighFive && !animacaoDado && !animacaoDance) {
      // Seleciona personalidade e humor para busca no jsonFrases
      String persKey = (personalidade == PERSONALIDADE_SARCASTICA) ? "sarcastica" :
                       (personalidade == PERSONALIDADE_SENSIVEL)   ? "sensivel"  : "normal";
      String humorKey = (humorAtual == HUMOR_DOENTE || humorAtual == HUMOR_CARENTE) ? "doente" :
                        (humorAtual == HUMOR_ENTEDIADO) ? "entediado" : "feliz";
      // Extrai o array de frases do JSON para a chave personalidade+humor
      // Formato: {"sarcastica":{"doente":["frase1","frase2",...], ...}, ...}
      String searchKey = "\"" + persKey + "\":{\"" + humorKey + "\":[";
      int startIdx = jsonFrases.indexOf(searchKey);
      textoFala = ""; // fallback vazio
      if (startIdx >= 0) {
        startIdx += searchKey.length();
        int endIdx = jsonFrases.indexOf("]", startIdx);
        if (endIdx > startIdx) {
          String arr = jsonFrases.substring(startIdx, endIdx); // "frase1","frase2",...
          // Conta quantas frases há
          int count = 1;
          for (int ci = 0; ci < (int)arr.length(); ci++) if (arr[ci] == ',') count++;
          int pick = random(0, count);
          // Pula 'pick' vírgulas entre aspas
          int q = 0;
          for (int skip = 0; skip < pick; skip++) {
            q = arr.indexOf(',', q) + 1;
          }
          int qs = arr.indexOf('"', q) + 1;
          int qe = arr.indexOf('"', qs);
          if (qs > 0 && qe > qs) textoFala = arr.substring(qs, qe);
        }
      }
      if (textoFala.length() == 0) textoFala = "OI!"; // fallback
      tempoFimFala = millis() + 4000; // Fala dura 4 segundos
    }
  } else {
    tempoFimFala = 0; // Reseta se não nasceu ou menor de 15 dias
  }

  // Trata conexões do painel web se WiFi ativo
  if (wifiConectado) {
    server.handleClient();
  }
  
  // Atualiza humor do pet
  calcularHumor();

  // Leitura do LDR e depuração serial completa de hardware (útil inclusive no estado de ovo)
  ldrValor = analogRead(ldrPin);
  luzPorcento = map(ldrValor, 0, 4095, 100, 0);

  // Calcula se o pet está dormindo (Luz baixa OU horário NTP entre 22h e 8h)
  petDormindo = false;
  if (luzPorcento < limiarLuzBaixo) {
    petDormindo = true;
  } else if (sonoAutomaticoHorario && wifiConectado) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int hora = timeinfo.tm_hour;
      if (hora >= 22 || hora < 8) {
        petDormindo = true;
      }
    }
  }

  static unsigned long tempoUltimoPrintDebug = 0;
  if (millis() - tempoUltimoPrintDebug >= 1000) {
    Serial.print(F("[DEBUG] LDR Raw: ")); Serial.print(ldrValor);
    Serial.print(F(" | Luz %: ")); Serial.print(luzPorcento);
    Serial.print(F(" | Choque (Balancou): ")); Serial.print(choqueDetectado ? "SIM" : "NAO");
    Serial.print(F(" | Obstaculo: ")); Serial.print(digitalRead(obstaculoPin) == LOW ? "SIM" : "NAO");
    Serial.print(F(" | Vinculo: ")); Serial.print(pontosDeVinculo);
    Serial.print(F(" | Dormindo: ")); Serial.print(petDormindo ? "SIM" : "NAO");
    Serial.print(F(" | Personalidade: "));
    if (personalidade == PERSONALIDADE_BASICA) Serial.println(F("BASICA"));
    else if (personalidade == PERSONALIDADE_SARCASTICA) Serial.println(F("SARCASTICA"));
    else if (personalidade == PERSONALIDADE_SENSIVEL) Serial.println(F("SENSIVEL"));
    
    choqueDetectado = false; // Limpa para a próxima janela de 1 segundo
    tempoUltimoPrintDebug = millis();
  }

  // Se ainda não nasceu, roda o fluxo de incubação do ovo e desenha na tela
  if (estadoNascimento == ESTADO_OVO || estadoNascimento == ESTADO_CHOCANDO) {
    processarFluxoIncubacao();
    return;
  }

  // --- 1. LÓGICA DO BOTÃO (Pressionamento Longo 4s para Silencioso / Transições) ---
  bool botaoPressionado = (digitalRead(botaoPin) == LOW);
  static bool botaoUltimoEstado = HIGH;
  static unsigned long tempoInicioPressionado = 0;
  static bool gatilhoSilenciosoDisparado = false;

  // Borda de descida (quando aperta o botão)
  if (botaoPressionado && botaoUltimoEstado == HIGH) {
    tempoInicioPressionado = millis();
    gatilhoSilenciosoDisparado = false;
    delay(50); // Debounce
  }
  
  // Enquanto o botão continuar pressionado, verifica se deu 4 segundos
  if (botaoPressionado && !gatilhoSilenciosoDisparado) {
    if (millis() - tempoInicioPressionado >= 4000) {
      modoSilencioso = !modoSilencioso;
      salvarEstado();
      
      feedbackSilenciosoMsg = modoSilencioso ? "MODO SILENCIOSO" : "SOM ATIVADO";
      tempoFimFeedbackSilencioso = millis() + 2000; // Feedback visual por 2s
      
      gatilhoSilenciosoDisparado = true;
      if (!modoSilencioso) {
        myTone(880, 150); // Bip curto de feedback ao ligar
      }
    }
  }

  // Borda de subida (quando solta o botão)
  if (botaoUltimoEstado == LOW && !botaoPressionado) {
    unsigned long duracaoPressionado = millis() - tempoInicioPressionado;
    
    // Só processa o clique curto se não chegou a disparar a ação de 4s
    if (duracaoPressionado < 4000) {
      if (modoPiano && idadeDias >= 21) {
        if (duracaoPressionado < 300) {
          myTone(523, 150); // C5 (Dó)
        } else if (duracaoPressionado < 800) {
          myTone(659, 150); // E5 (Mi)
        } else {
          myTone(784, 250); // G5 (Sol)
        }
      } else {
        // Modo transição de telas normal
        if (telaAtual == TELA_FACE) {
          mudarTelaPara(TELA_TEMP);
          tempoBotaoPressionado = millis();
        } else {
          mudarTelaPara(TELA_FACE);
        }
      }
    }
    delay(50); // Debounce
  }
  botaoUltimoEstado = botaoPressionado;

  // Transições Automáticas de Tempo (só ativa se não estiver em transição)
  if (!emTransicao) {
    if (telaAtual == TELA_TEMP) {
      if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
        mudarTelaPara(TELA_UMID);
        tempoBotaoPressionado = millis(); // Reinicia o timer para a próxima tela
      }
    } 
    else if (telaAtual == TELA_UMID) {
      if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
        mudarTelaPara(TELA_CLOCK);
        tempoBotaoPressionado = millis();
      }
    }
    else if (telaAtual == TELA_CLOCK) {
      if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
        mudarTelaPara(TELA_IDADE);
        tempoBotaoPressionado = millis();
      }
    }
    else if (telaAtual == TELA_IDADE) {
      if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
        mudarTelaPara(TELA_PET_STATUS);
        tempoBotaoPressionado = millis();
      }
    }
    else if (telaAtual == TELA_PET_STATUS) {
      if (millis() - tempoBotaoPressionado >= TEMPO_TELA_INFO) {
        mudarTelaPara(TELA_FACE);
      }
    }
  }

  // --- 2. DECAIMENTO DOS ATRIBUTOS DO PET ---
  if (petTagPresente) {
    diversao = 100; // Impede que sinta tédio/vontade de brincar enquanto estiver com o gatinho
  }
  // Fome e diversão só decaem se o pet estiver acordado (!petDormindo)
  if (!petDormindo) {
    unsigned long intervaloFomeAtual = INTERVALO_FOME;
    if (personalidadeNatural == PERSONALIDADE_SENSIVEL) {
      intervaloFomeAtual = (INTERVALO_FOME * 2) / 3; // 1.5x mais rápido
    }
    if (idadeDias >= 30) {
      intervaloFomeAtual = (intervaloFomeAtual * 10) / 7; // ~30% mais lento
    }
    if (millis() - tempoUltimaFome >= intervaloFomeAtual) {
      fome = max(0, fome - 1);
      tempoUltimaFome = millis();
      salvarEstado();
    }
    
    unsigned long intervaloBrincarAtual = INTERVALO_BRINCAR;
    if (personalidadeNatural == PERSONALIDADE_SENSIVEL) {
      intervaloBrincarAtual = (INTERVALO_BRINCAR * 2) / 3; // 1.5x mais rápido
    } else if (personalidadeNatural == PERSONALIDADE_SARCASTICA) {
      intervaloBrincarAtual = (INTERVALO_BRINCAR * 5) / 3; // ~0.6x da velocidade (1.66x mais devagar)
    }
    if (idadeDias >= 30) {
      intervaloBrincarAtual = (intervaloBrincarAtual * 10) / 7; // ~30% mais lento
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
  if (personalidadeNatural == PERSONALIDADE_SENSIVEL) {
    intervaloSaudeAtual = INTERVALO_SAUDE / 2; // 2x mais rápido
  }
  if (millis() - tempoUltimaSaude >= intervaloSaudeAtual) {
    // Saúde decai se o pet estiver faminto E entediado, OU se estiver doente (estaDoente)
    int limiarFome = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
    int limiarDiversao = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
    if ((fome <= limiarFome && diversao <= limiarDiversao) || estaDoente) {
      saude = max(0, saude - 1);
      salvarEstado();
    }
    tempoUltimaSaude = millis();
  }

  // --- 3. ALERTA PERIÓDICO DE ATENÇÃO DO BUZZER ---
  int limiarAlerta = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
  int limiarUrgente = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 25 : 15;
  if (fome <= limiarAlerta || diversao <= limiarAlerta || saude <= limiarAlerta) {
    if (millis() - tempoUltimoAlerta >= INTERVALO_ALERTA) {
      if (fome <= limiarUrgente || diversao <= limiarUrgente || saude <= limiarUrgente) {
        // Tom irritado/urgente se estiver bravo (bip-bip rápido e agudo)
        myTone(523, 100); delay(150);
        myTone(523, 100);
      } else {
        // Tom melancólico duplo (bip-bip)
        myTone(330, 150); delay(200);
        myTone(262, 250);
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
  // Se o robô ficar no frio (temperatura < limiarTempBaixo), roda uma probabilidade pequena a cada 1 minuto (60 segundos)
  // de ficar resfriado (média de 1 vez por semana considerando 8 horas de frio por noite = 1 chance em 3360 por minuto)
  if (temperatura < limiarTempBaixo && temperatura > 0 && !estaDoente) {
    if (tempoNoFrio == 0) {
      tempoNoFrio = millis();
    } else if (millis() - tempoNoFrio >= 60000) {
      tempoNoFrio = millis(); // Reinicia o timer de 1 minuto para a próxima verificação
      if (random(0, 3360) == 0) {
        estaDoente = true;
        salvarEstado();
        // Melodia triste de doença (RTTTL)
        playRTTTL(rtttl_sick_melody);
      }
    }
  } else if (temperatura >= limiarTempBaixo || temperatura <= 0) {
    tempoNoFrio = 0;
  }

  // Lógica do Espirro (se estiver doente, espirra de tempos em tempos)
  if (estaDoente) {
    if (millis() - tempoUltimoEspirro >= tempoProximoEspirro) {
      // Ativa animação de espirro
      animacaoEspirro = true;
      tempoInicioEspirro = millis();
      
      // Som de espirro (A-choo!)
      myTone(880, 80); delay(100);
      myTone(440, 150);
      
      tempoUltimoEspirro = millis();
      tempoProximoEspirro = random(12000, 25000); // Espirra a cada 12 a 25s
    }
  }

  // --- 4. LEITURA DO LEITOR RFID (Com Auto-Aprendizado) ---
  bool leuCartao = false;
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    leuCartao = true;
    byte* cardUID = rfid.uid.uidByte;
    
    // Alimenta o buffer do ESP-NOW para enviar à cabeça
    rfidLidoPendente = true;
    memcpy(rfidUIDPendente, cardUID, 4);
    
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
      rfidAcaoPendente = 1;
    }
    else if (!eUIDZerado(uidBrincar) && comparaUID(cardUID, uidBrincar)) {
      brincarPet();
      rfidAcaoPendente = 2;
    }
    else if (!eUIDZerado(uidSaude) && comparaUID(cardUID, uidSaude)) {
      curarPet();
      rfidAcaoPendente = 3;
    }
    else if (!eUIDZerado(uidPet) && comparaUID(cardUID, uidPet)) {
      acariciarPet();
      petTagPresente = true;
      rfidAcaoPendente = 4;
    }
    else {
      // Aprende o cartão na ordem
      if (eUIDZerado(uidComida)) {
        copiaUID(uidComida, cardUID);
        Serial.println(F("-> Cartao registrado para COMIDA!"));
        mostrarMensagemRegistro("COMIDA");
        alimentarPet();
        rfidAcaoPendente = 1;
      }
      else if (eUIDZerado(uidBrincar)) {
        if (!comparaUID(cardUID, uidComida)) {
          copiaUID(uidBrincar, cardUID);
          Serial.println(F("-> Cartao registrado para BRINCAR!"));
          mostrarMensagemRegistro("BRINCAR");
          brincarPet();
          rfidAcaoPendente = 2;
        }
      }
      else if (eUIDZerado(uidSaude)) {
        if (!comparaUID(cardUID, uidComida) && !comparaUID(cardUID, uidBrincar)) {
          copiaUID(uidSaude, cardUID);
          Serial.println(F("-> Cartao registrado para SAUDE!"));
          mostrarMensagemRegistro("SAUDE");
          curarPet();
          rfidAcaoPendente = 3;
        }
      }
      else if (eUIDZerado(uidPet)) {
        if (!comparaUID(cardUID, uidComida) && !comparaUID(cardUID, uidBrincar) && !comparaUID(cardUID, uidSaude)) {
          copiaUID(uidPet, cardUID);
          Serial.println(F("-> Cartao registrado para PET!"));
          mostrarMensagemRegistro("GATINHO (PET)");
          acariciarPet();
          petTagPresente = true;
          rfidAcaoPendente = 4;
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
  if (millis() >= tempoFimTesteFace) {
    tempoFimTesteFace = 0;
    if (animacaoSusto && (millis() - tempoInicioSusto >= DURACAO_SUSTO)) {
      animacaoSusto = false;
    }
    if (animacaoIrritado && (millis() - tempoInicioIrritado >= DURACAO_IRRITADO)) {
      animacaoIrritado = false;
    }
    if (animacaoEspirro && (millis() - tempoInicioEspirro >= 1500)) {
      animacaoEspirro = false;
    }
    if (animacaoHighFive && (millis() - tempoInicioHighFive >= 2000)) {
      animacaoHighFive = false;
    }
    if (animacaoDado && (millis() - tempoInicioDado >= 2500)) {
      animacaoDado = false;
    }
    if (animacaoDance && (millis() - tempoInicioDance >= 3000)) {
      animacaoDance = false;
    }
    if ((animacaoComendo || animacaoBrincando || animacaoCurando || animacaoAcariciado) && 
        (millis() - tempoInicioReacao >= DURACAO_REACAO)) {
      animacaoComendo = false;
      animacaoBrincando = false;
      animacaoCurando = false;
      animacaoAcariciado = false;
    }
  }

  // --- 4. LEITURA DOS SENSORES ---
  // Captura o sinal de som em uma janela de ~4ms (80 amostras com 50us) e calcula o desvio pico-a-pico
  // para cancelar o DC offset e capturar a amplitude real da onda sonora
  int maxLeitura = 0;
  int minLeitura = 4095;
  for (int i = 0; i < 80; i++) {
    int leitura = analogRead(somAnalogPin);
    if (leitura > maxLeitura) maxLeitura = leitura;
    if (leitura < minLeitura) minLeitura = leitura;
    delayMicroseconds(50);
  }
  int somMaxDev = maxLeitura - minLeitura;
  somMaxDevGlobal = somMaxDev;

  // Lógica de reação ao som baseada nos limiares ajustados (400=médio, 1000=forte)
  static unsigned long tempoUltimaReacaoSom = 0;
  if (somMaxDev >= limiarSusto) {
    if (!animacaoSusto && (millis() - tempoUltimaReacaoSom > 2000)) {
      tempoUltimaReacaoSom = millis();
      Serial.println(F("Som ALTO detectado! Susto!"));
      animacaoSusto = true;
      tempoInicioSusto = millis();
      mudarTelaPara(TELA_FACE);
      atualizarVinculo(-2); // Perde -2 pontos de vínculo ao ser assustado
    }
  }
  else if (somMaxDev >= limiarBrincar) {
    if (idadeDias >= 3 && !animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoSusto && !animacaoIrritado && !animacaoDance) {
      static int contadorPalmas = 0;
      static unsigned long tempoUltimaPalma = 0;
      if (millis() - tempoUltimaPalma > 200 && millis() - tempoUltimaPalma < 1000) {
        contadorPalmas++;
        if (contadorPalmas >= 3) {
          animacaoDance = true;
          tempoInicioDance = millis();
          contadorPalmas = 0;
          playRTTTL("party:d=16,o=6,b=200:c,e,g,c,e,g"); 
        }
      } else if (millis() - tempoUltimaPalma >= 1000) {
        contadorPalmas = 1;
      }
      tempoUltimaPalma = millis();
    }
    
    if (!animacaoDance && !animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoSusto && !animacaoIrritado && (millis() - tempoUltimaReacaoSom > 6000)) {
      tempoUltimaReacaoSom = millis();
      int limiarTriste = (personalidadeNatural == PERSONALIDADE_SENSIVEL) ? 50 : 30;
      if (personalidade == PERSONALIDADE_SARCASTICA || fome <= limiarTriste) {
        Serial.println(F("Som intermediario detectado! Pet fica IRRITADO!"));
        animacaoIrritado = true;
        tempoInicioIrritado = millis();
        mudarTelaPara(TELA_FACE);
      } else {
        Serial.println(F("Som intermediario detectado! Pet fica BRINCANDO!"));
        animacaoBrincando = true;
        tempoInicioReacao = millis();
        mudarTelaPara(TELA_FACE);
      }
    }
  }

  // Sensor de Obstáculo IR (Vias Digitais - LOW indica obstáculo próximo)
  distancia = 999;
  if (digitalRead(obstaculoPin) == LOW) {
    if (idadeDias >= 2 && !animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoAcariciado && !animacaoHighFive && !animacaoDado && !animacaoDance) {
      animacaoHighFive = true;
      tempoInicioHighFive = millis();
      myTone(880, 100); delay(150); myTone(1047, 200);
    } else {
      distancia = 5; // Simula proximidade (< 10cm) para disparar reação
    }
  }

  // Sensor de Inclinação (Detecta movimento/chacoalho por mudança de estado físico)
  static int ultimoEstadoInclinacao = HIGH;
  int estadoInclinacao = digitalRead(choquePin);
  if (estadoInclinacao != ultimoEstadoInclinacao) {
    choqueDetectado = true; // Trava para o log de depuração
    if (idadeDias >= 5 && !animacaoComendo && !animacaoBrincando && !animacaoCurando && !animacaoAcariciado && !animacaoHighFive && !animacaoDado && !animacaoDance) {
      animacaoDado = true;
      tempoInicioDado = millis();
      valorDado = random(1, 7);
      myTone(150, 50); delay(70);
      myTone(250, 50);
    } else {
      variacaoMovimento = 10000; // Define variação alta (> 8000) para tontura
    }
    tempoUltimoChoque = millis();
    ultimoEstadoInclinacao = estadoInclinacao;
  } else {
    // Reduz a tontura se ficar 2.0 segundos sem receber novos movimentos/inclinações
    if (millis() - tempoUltimoChoque > 2000) {
      variacaoMovimento = 0;
    }
  }

  if (millis() - tempoUltimaLeituraDHT >= 2000) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      temperatura = t; umidade = h;
    }
    tempoUltimaLeituraDHT = millis();
  }



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

  // Movimento Ocioso de Olhar ao Redor
  if (!estaPiscando && !petDormindo) {
    if (millis() - tempoUltimoOlharOcioso > tempoProximoOlharOcioso) {
      if (personalidade == PERSONALIDADE_SARCASTICA) {
        // Sequência de revirar os olhos (ciclo de olhar em volta)
        static byte faseRolada = 0;
        faseRolada = (faseRolada + 1) % 5;
        if (faseRolada == 0) { ociosoLookX = 0; ociosoLookY = 0; tempoProximoOlharOcioso = random(3000, 5000); }
        else if (faseRolada == 1) { ociosoLookX = -7; ociosoLookY = 2; tempoProximoOlharOcioso = 800; }
        else if (faseRolada == 2) { ociosoLookX = -6; ociosoLookY = -4; tempoProximoOlharOcioso = 600; }
        else if (faseRolada == 3) { ociosoLookX = 6; ociosoLookY = -4; tempoProximoOlharOcioso = 600; }
        else if (faseRolada == 4) { ociosoLookX = 7; ociosoLookY = 2; tempoProximoOlharOcioso = 800; }
      } else {
        int chanceOlhar = random(0, 100);
        if (chanceOlhar < 35) {
          // Olha para a esquerda de forma bem perceptível
          ociosoLookX = random(-14, -7);
          ociosoLookY = random(-2, 3);
        } else if (chanceOlhar < 70) {
          // Olha para a direita de forma bem perceptível
          ociosoLookX = random(8, 15);
          ociosoLookY = random(-2, 3);
        } else if (chanceOlhar < 85) {
          // Olha para cima/baixo
          ociosoLookX = random(-3, 4);
          ociosoLookY = random(-6, 7);
        } else {
          // Olha para o centro
          ociosoLookX = 0;
          ociosoLookY = 0;
        }
        tempoProximoOlharOcioso = random(6000, 15000);
      }
      tempoUltimoOlharOcioso = millis();
    }
  } else {
    ociosoLookX = 0.0;
    ociosoLookY = 0.0;
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
  else if (fome <= (personalidadeNatural == PERSONALIDADE_SENSIVEL ? 25 : 15) || 
           diversao <= (personalidadeNatural == PERSONALIDADE_SENSIVEL ? 25 : 15) || 
           saude <= (personalidadeNatural == PERSONALIDADE_SENSIVEL ? 25 : 15)) {
    // BRAVO / MUITO NEGLIGENCIADO (Sem atenção por muito tempo)
    targetLw = 20.0; targetLh = 18.0;
    targetRw = 20.0; targetRh = 18.0;
    targetRadius = 4.0;
    
    // Animação de respiração raivosa rápida + pequeno tremor lateral
    float tremorX = sin(millis() * 0.02) * 0.5;
    float breathY = sin(millis() * 0.008) * 0.8;
    targetLx = baseLx + tremorX + ociosoLookX * 0.5;
    targetLy = baseLy + 1.0 + breathY + ociosoLookY * 0.5;
    targetRx = baseRx + tremorX + ociosoLookX * 0.5;
    targetRy = baseRy + 1.0 + breathY + ociosoLookY * 0.5;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  }
  else if (fome <= (personalidadeNatural == PERSONALIDADE_SENSIVEL ? 50 : 30) || 
           diversao <= (personalidadeNatural == PERSONALIDADE_SENSIVEL ? 50 : 30) || 
           saude <= (personalidadeNatural == PERSONALIDADE_SENSIVEL ? 50 : 30) || 
           estaDoente) {
    // TRISTE / FAMINTO / ENTEDIADO / DOENTE
    targetLw = 20.0; targetLh = 16.0;
    targetRw = 20.0; targetRh = 16.0;
    targetRadius = 4.0;
    
    // Animação de respiração triste lenta e olhos ligeiramente caídos
    float breathY = sin(millis() * 0.0025) * 1.0;
    targetLx = baseLx + ociosoLookX * 0.6;
    targetLy = baseLy + 3.0 + breathY + ociosoLookY * 0.6;
    targetRx = baseRx + ociosoLookX * 0.6;
    targetRy = baseRy + 3.0 + breathY + ociosoLookY * 0.6;

    suor1.ativa = false; suor2.ativa = false;
    zZzVal.ativa = false;

    // Se for sensível, chora (gotas de lágrima) com facilidade
    if (personalidadeNatural == PERSONALIDADE_SENSIVEL) {
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
  else if (petDormindo) {
    // SONO: Dormindo automaticamente por horario ou escuro
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
  else if (luzPorcento > limiarLuzAlto) {
    // CLARO / ESPREMIDO: Luz muito forte
    targetLw = 24.0; targetLh = 8.0;
    targetRw = 24.0; targetRh = 8.0;
    targetRadius = 2.0;
    targetLx = baseLx + ociosoLookX * 0.4; targetLy = baseLy + 3.0 + ociosoLookY * 0.4;
    targetRx = baseRx + ociosoLookX * 0.4; targetRy = baseRy + 3.0 + ociosoLookY * 0.4;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (temperatura > limiarTempAlto) {
    // QUENTE: Temperatura alta
    targetLw = 20.0; targetLh = 22.0; 
    targetRw = 20.0; targetRh = 22.0;
    targetRadius = 6.0;
    targetLx = baseLx + ociosoLookX * 0.5; targetLy = baseLy + 4.0 + ociosoLookY * 0.5;
    targetRx = baseRx + ociosoLookX * 0.5; targetRy = baseRy + 4.0 + ociosoLookY * 0.5;

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
  else if (temperatura < limiarTempBaixo && temperatura > 0) {
    // FRIO: Temperatura baixa
    targetLw = 12.0; targetLh = 12.0;
    targetRw = 12.0; targetRh = 12.0;
    targetRadius = 4.0;
    
    // Tremor de frio
    targetLx = baseLx + random(-2, 3) + ociosoLookX * 0.6;
    targetLy = baseLy + random(-2, 3) + ociosoLookY * 0.6;
    targetRx = baseRx + random(-2, 3) + ociosoLookX * 0.6;
    targetRy = baseRy + random(-2, 3) + ociosoLookY * 0.6;

    suor1.ativa = false; suor2.ativa = false;
    lagrimaL.ativa = false; lagrimaR.ativa = false;
    zZzVal.ativa = false;
  } 
  else if (umidade > 75) {
    // ÚMIDO: Olhos chorosos / molhados
    targetLw = 22.0; targetLh = 32.0;
    targetRw = 22.0; targetRh = 32.0;
    targetRadius = 9.0;
    targetLx = baseLx + ociosoLookX * 0.7; targetLy = baseLy - 2.0 + ociosoLookY * 0.7; 
    targetRx = baseRx + ociosoLookX * 0.7; targetRy = baseRy - 2.0 + ociosoLookY * 0.7; 

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
    targetLx = baseLx + ociosoLookX;
    targetLy = baseLy + ociosoLookY;
    targetRx = baseRx + ociosoLookX;
    targetRy = baseRy + ociosoLookY;
  }

  // --- LÓGICA DE PISCADA INDEPENDENTE DA EXPRESSÃO ---
  // A piscada só acontece se o robô estiver acordado (luzPorcento >= limiarLuzBaixo)
  if (luzPorcento >= limiarLuzBaixo) {
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

  // Roda animação do gatinho mini andando (fora do loop de renderização para rodar apenas uma vez por frame)
  if (petTagPresente && !animacaoAcariciado && luzPorcento >= limiarLuzBaixo) {
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


  // LÓGICA DE MICRO-ANIMAÇÕES OCIOSAS (TICK)
  if (telaAtual == TELA_FACE && !emTransicao && !petDormindo && 
      !animacaoComendo && !animacaoBrincando && !animacaoCurando && 
      !animacaoSusto && !animacaoAcariciado && !animacaoEspirro && !animacaoIrritado &&
      !animacaoHighFive && !animacaoDado && !animacaoDance) {
      
    // Se a animação atual terminou, reseta tipo
    if (tipoReacaoOciosa != 0 && millis() > tempoFimReacaoOciosa) {
      tipoReacaoOciosa = 0;
      tempoProximaReacaoOciosa = millis() + random(25000, 45000); // Aguarda de 25 a 45 segundos para a próxima
    }
    
    // Se está na hora da próxima reação e não tem nenhuma rodando
    if (tipoReacaoOciosa == 0 && millis() > tempoProximaReacaoOciosa) {
      int r = random(0, 100);
      
      if (personalidade == PERSONALIDADE_SARCASTICA) {
        if (r < 33) {
          tipoReacaoOciosa = 2; // Revirar olhos (2 segundos) [EXISTENTE]
          tempoFimReacaoOciosa = millis() + 2000;
        } else if (r < 66) {
          tipoReacaoOciosa = 8; // Olhos >< e boca meia lua para baixo (2 segundos) [NOVA]
          tempoFimReacaoOciosa = millis() + 2000;
        } else {
          tipoReacaoOciosa = 9; // Olhos - - e boca VVV (2 segundos) [NOVA]
          tempoFimReacaoOciosa = millis() + 2000;
        }
      } 
      else if (personalidade == PERSONALIDADE_SENSIVEL) {
        if (temperatura < limiarTempBaixo - 1.0 && temperatura > 0) {
          // Se estiver frio, treme
          tipoReacaoOciosa = 4; // Tremedeira (2.5 segundos) [EXISTENTE]
          tempoFimReacaoOciosa = millis() + 2500;
        } else {
          if (r < 33) {
            tipoReacaoOciosa = 12; // Olhos coração e boca meia lua para cima (2 segundos) [NOVA]
            tempoFimReacaoOciosa = millis() + 2000;
          } else if (r < 66) {
            tipoReacaoOciosa = 7; // Sorridente com corações subindo (2 segundos) [EXISTENTE]
            tempoFimReacaoOciosa = millis() + 2000;
          } else {
            tipoReacaoOciosa = 14; // Mandando beijo (2 segundos) [NOVA]
            tempoFimReacaoOciosa = millis() + 2000;
          }
        }
      } 
      else { // PERSONALIDADE_BASICA (Normal)
        if (r < 33) {
          tipoReacaoOciosa = 1; // Assobio (2.5 segundos) [EXISTENTE]
          tempoFimReacaoOciosa = millis() + 2500;
          if (idadeDias >= 1) {
            playRTTTL(rtttl_mario);
          }
        } else if (r < 66) {
          tipoReacaoOciosa = 10; // Piscadela (2 segundos) [NOVA]
          tempoFimReacaoOciosa = millis() + 2000;
        } else {
          tipoReacaoOciosa = 11; // Olhos >< e boca meia lua para cima (2 segundos) [NOVA]
          tempoFimReacaoOciosa = millis() + 2000;
        }
      }
    }
  } else {
    // Se saiu da tela principal ou entrou em animação de cuidado, cancela ócio e adia próxima reação
    tipoReacaoOciosa = 0;
    tempoProximaReacaoOciosa = millis() + random(25000, 45000);
  }

  // Atualiza as partículas físicas do pet
  spawnarParticulasOciosas();
  atualizarParticulas();

  // --- 4. RENDERIZAÇÃO DA TELA (Com transição de slide) ---
  int offset = 0;
  bool forward = true;
  if (emTransicao) {
    unsigned long elapsed = millis() - tempoInicioTransicao;
    if (elapsed >= DURACAO_TRANSICAO) {
      emTransicao = false;
      telaAtual = telaDestino;
      offset = 0;
    } else {
      float t = (float)elapsed / DURACAO_TRANSICAO;
      float progress = t * (2 - t); // Ease-out quadratic
      offset = (int)(progress * 128.0);
      
      // Determina direção do slide
      if (telaDestino == TELA_FACE && telaOrigem == TELA_PET_STATUS) forward = true;
      else if (telaDestino == TELA_TEMP && telaOrigem == TELA_FACE) forward = true;
      else if (telaDestino > telaOrigem) forward = true;
      else forward = false;
    }
  }

  u8g2.firstPage();
  do {
    if (millis() < tempoFimFeedbackSilencioso) {
      u8g2.setFont(u8g2_font_6x12_tr);
      int strWidth = u8g2.getStrWidth(feedbackSilenciosoMsg.c_str());
      u8g2.drawStr((128 - strWidth) / 2, 36, feedbackSilenciosoMsg.c_str());
      u8g2.drawFrame(10, 15, 108, 34);
      int typeWidth = u8g2.getStrWidth((feedbackSilenciosoMsg.indexOf("SILENCIOSO") != -1) ? "MUTED" : "SOUND");
      u8g2.drawStr((128 - typeWidth) / 2, 27, (feedbackSilenciosoMsg.indexOf("SILENCIOSO") != -1) ? "MUTED" : "SOUND");
    } else if (emTransicao) {
      if (forward) {
        // Desliza da direita para a esquerda:
        // Tela de origem sai para a esquerda (-offset)
        // Tela de destino entra pela direita (128 - offset)
        desenharTelas(-offset, telaOrigem);
        desenharTelas(128 - offset, telaDestino);
      } else {
        // Desliza da esquerda para a direita:
        // Tela de origem sai para a direita (offset)
        // Tela de destino entra pela esquerda (-128 + offset)
        desenharTelas(offset, telaOrigem);
        desenharTelas(-128 + offset, telaDestino);
      }
    } else {
      desenharTelas(0, telaAtual);
    }
  } while ( u8g2.nextPage() );

  // Envia o estado via ESP-NOW a cada 200ms
  if (millis() - ultimoEnvioEspNow >= 200) {
    enviarEstadoPorEspNow();
    ultimoEnvioEspNow = millis();
  }

  // Ajustado o delay para manter as transições fluidas (~30 FPS)
  delay(35); 
}
