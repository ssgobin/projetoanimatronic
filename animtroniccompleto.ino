#include <WiFi.h>
#include <PubSubClient.h>
#include "DFRobotDFPlayerMini.h"
#include <ESP32Servo.h>

// Config WiFi e MQTT
const char* ssid = "iPhone de Kauan";
const char* password = "senhadificildapega";
const char* mqtt_server = "test.mosquitto.org";

// Definição de pinos
#define pinRx 16   // RX2 (GPIO16) - Você pode trocar se quiser
#define pinTx 17   // TX2 (GPIO17)
#define SERVO_PIN 18 // GPIO18

Servo motorServo;
HardwareSerial playerMP3Serial(2);  // Usando Serial2 do ESP32
DFRobotDFPlayerMini playerMP3;

WiFiClient espClient;
PubSubClient client(espClient);

const char* topicPlay = "dfplayer/music/play";
const char* topicVolume = "dfplayer/volume/set";
const char* topicStop = "dfplayer/music/stop";
const char* topicLoop = "dfplayer/music/loop";
const char* topicAuto = "dfplayer/music/auto";
const char* topicServoOn = "servo/on";
const char* topicServoOff = "servo/off";
const char* topicVigilancia = "servo/vigilancia";
const char* topicPreset = "dfplayer/preset";
const char* topicPresetStop = "dfplayer/preset/stop";

bool servoAtivo = false;
unsigned long ultimoMovimento = 0;
int estadoServo = 0;
int pasta = 1;
int musica = 1;
int estadoAnterior = -1;
bool repetirMusica = false;
bool modoAutomatico = false;
bool presetExecutando = false;
bool interromperPreset = false;
bool modoVigilancia = false;
String clientId;
static unsigned long tempoInicioGiro = 0;
static bool girandoDireita = true;
static bool executandoGiro = false;

void moverServo(int anguloInicial, int anguloFinal, int tempoMS, int repeticoes) {
  for (int r = 0; r < repeticoes && !interromperPreset; r++) {
    for (int ang = anguloInicial; ang <= anguloFinal && !interromperPreset; ang++) {
      motorServo.write(ang);
      delay(tempoMS);
    }
    for (int ang = anguloFinal; ang >= anguloInicial && !interromperPreset; ang--) {
      motorServo.write(ang);
      delay(tempoMS);
    }
  }
}

void moverServoDuranteMusica(int anguloInicial, int anguloFinal, int tempoMS) {
  if (!motorServo.attached()) motorServo.attach(SERVO_PIN);
  bool subindo = true;
  int angulo = anguloInicial;

  while (playerMP3.readState() != 512 && !interromperPreset) {
    motorServo.write(angulo);
    delay(tempoMS);
    if (subindo) {
      angulo++;
      if (angulo >= anguloFinal) subindo = false;
    } else {
      angulo--;
      if (angulo <= anguloInicial) subindo = true;
    }
  }
  motorServo.detach();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.printf("[MQTT] Tópico: %s | Mensagem: %s\n", topic, msg.c_str());

  if (String(topic) == topicPlay) {
    int idx = msg.indexOf(',');
    if (idx > 0) {
      pasta = msg.substring(0, idx).toInt();
      musica = msg.substring(idx + 1).toInt();
      playerMP3.playFolder(pasta, musica);
    }
  } else if (String(topic) == topicVolume) {
    playerMP3.volume(msg.toInt());
  } else if (String(topic) == topicStop) {
    playerMP3.stop();
  } else if (String(topic) == topicLoop) {
    repetirMusica = msg == "on";
  } else if (String(topic) == topicAuto) {
    modoAutomatico = msg == "on";
  } else if (String(topic) == topicServoOn) {
    servoAtivo = true;
  } else if (String(topic) == topicServoOff) {
    servoAtivo = false;
    motorServo.detach();
  } else if (msg == "stop") {
    interromperPreset = true;
    playerMP3.stop();
    motorServo.detach();
    presetExecutando = false;
  } else if (String(topic) == topicVigilancia) {
    modoVigilancia = (msg == "on");
    if (modoVigilancia) {
      servoAtivo = false;
      if (!motorServo.attached()) motorServo.attach(SERVO_PIN);
    } else {
      motorServo.detach();
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando ao MQTT...");
    if (client.connect(clientId.c_str())) {
      Serial.println("Conectado");
      playerMP3.playFolder(3, 1);
      client.subscribe(topicPlay);
      client.subscribe(topicVolume);
      client.subscribe(topicStop);
      client.subscribe(topicLoop);
      client.subscribe(topicAuto);
      client.subscribe(topicServoOn);
      client.subscribe(topicServoOff);
      client.subscribe(topicPreset);
      client.subscribe(topicPresetStop);
      client.subscribe(topicVigilancia);
    } else {
      Serial.printf("Falha, rc=%d. Tentando novamente em 5s\n", client.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  playerMP3Serial.begin(9600, SERIAL_8N1, pinRx, pinTx);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");

  if (!playerMP3.begin(playerMP3Serial)) {
    Serial.println("Erro ao iniciar DFPlayer! Reiniciando ESP em 5s...");
    delay(5000);
    ESP.restart();
  }

  playerMP3.volume(35);
  playerMP3.playFolder(3, 2);
  playerMP3.playFolder(3, 3);

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  clientId = "ESP32Client-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  motorServo.attach(SERVO_PIN);
  motorServo.write(90);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  static unsigned long ultimoCheck = 0;
  if (millis() - ultimoCheck > 1000) {
    ultimoCheck = millis();
    int estadoAtual = playerMP3.readState();

    if (estadoAnterior != 512 && estadoAtual == 512) {
      if (repetirMusica) {
        playerMP3.playFolder(pasta, musica);
      } else if (modoAutomatico) {
        musica++;
        if (musica > 30) musica = 1;
        playerMP3.playFolder(pasta, musica);
      }
    }
    estadoAnterior = estadoAtual;
  }

  if (servoAtivo) {
  if (!executandoGiro) {
    // Inicia o próximo giro
    tempoInicioGiro = millis();
    executandoGiro = true;

    if (girandoDireita) {
      motorServo.write(180);  // Gira para direita
      Serial.println("Girando 360 para DIREITA");
    } else {
      motorServo.write(0);    // Gira para esquerda
      Serial.println("Girando 360 para ESQUERDA");
    }
  } else {
    // Verifica se já passou o tempo de um giro de 360°
    unsigned long tempoGiro = 2000;  // Ajuste esse valor para o tempo que seu servo leva pra fazer 360 graus (em milissegundos)

    if (millis() - tempoInicioGiro >= tempoGiro) {
      motorServo.write(90);  // Para o motor
      Serial.println("Parando servo...");

      // Alterna a direção pro próximo ciclo
      girandoDireita = !girandoDireita;
      executandoGiro = false;

      delay(500);  // Pequena pausa entre os giros (se quiser)
    }
  }
} else {
  motorServo.write(90);  // Se o modo estiver desativado, garante que o servo pare
  executandoGiro = false;
}

static unsigned long tempoAnterior = 0;
static int anguloAtual = 0;
static bool indoDireita = true;

if (modoVigilancia) {
  unsigned long agora = millis();
  if (!motorServo.attached()) motorServo.attach(SERVO_PIN);

  int passoAngulo = 2; // tamanho do passo (quanto maior, mais rápido gira)
  int intervaloTempo = 20; // tempo entre cada passo (em ms)

  if (agora - tempoAnterior >= intervaloTempo) {
    tempoAnterior = agora;

    if (indoDireita) {
      anguloAtual += passoAngulo;
      if (anguloAtual >= 360) {
        anguloAtual = 360;
        indoDireita = false;
      }
    } else {
      anguloAtual -= passoAngulo;
      if (anguloAtual <= 0) {
        anguloAtual = 0;
        indoDireita = true;
      }
    }

    motorServo.write(anguloAtual);  // Só funcionará se seu servo aceitar 360 graus
    Serial.println(anguloAtual);    // Para você ver o valor no monitor serial
  }
}
}
