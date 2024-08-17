const char *version = "1.00"; // Versión del programa 
// Adaptación para la Trivia de los 3 Magos

#include "AudioFileSourceSD.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2SNoDAC.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESP32Servo.h>

bool OTAhabilitado = false;

// Configuración de la red WiFi
const char *ssid = "";
const char *password = "";

// Pines del Bus SPI para la conexión de la Tarjeta SD
#define SCK 18
#define MISO 19
#define MOSI 23
#define CS 5

// Pines para los pulsadores (ahora activos en bajo)
#define PIN_BOTON_1 4
#define PIN_BOTON_2 15
#define PIN_BOTON_3 34
#define PIN_BOTON_4 35

// Pines para los servomotores
#define PIN_SERVO_CUSPIDE 13
#define PIN_SERVO_BOCA 14

// Pines para LED RGB
#define PIN_LED_ROJO 25
#define PIN_LED_VERDE 26
#define PIN_LED_AZUL 27

#define CANAL_LEDC_0 0
#define CANAL_LEDC_1 1
#define CANAL_LEDC_2 2

#define LEDC_TIMER_8_BIT 8
#define FRECUENCIA_BASE_LEDC 5000

// Objetos Servo
Servo servoCuspide;
Servo servoBoca;

bool pulsadorPresionado = false;

// Variables para el estado de los pulsadores y manejo del debounce
bool pulsadoresPresionados[4] = {false, false, false, false};
unsigned long ultimosTiemposPulsadores[4] = {0, 0, 0, 0};
const unsigned long debounceDelay = 120;

#define PWM_PIN 12

// Variables para el manejo del audio
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *fuente;
AudioOutputI2SNoDAC *salida;
bool yaReprodujo = false;

// Variables para el manejo de la lógica del cuestionario
int rubroSeleccionado = -1;
const int totalRubros = 4;
const int preguntasPorRubro = 30;
const int preguntasPorJuego = 5;
int preguntasSeleccionadas[preguntasPorJuego];
int preguntaActual = 0;
int respuestasCorrectas = 0;

// Variables para el control de los servomotores
unsigned long ultimoMovimientoCuspide = 0;
unsigned long ultimoMovimientoBoca = 0;
const int intervaloMovimientoCuspide = 500;
const int intervaloMovimientoBoca = 100;

void setup() {
  Serial.begin(115200);
  Serial.println(version);

  if (!SD.begin(CS)) {
    Serial.println("Tarjeta SD no encontrada");
    return;
  }

  randomSeed(analogRead(0));

  pinMode(PIN_BOTON_1, INPUT_PULLUP);
  pinMode(PIN_BOTON_2, INPUT_PULLUP);
  pinMode(PIN_BOTON_3, INPUT_PULLUP);
  pinMode(PIN_BOTON_4, INPUT_PULLUP);
  
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoCuspide.setPeriodHertz(50);
  servoBoca.setPeriodHertz(50);
  servoCuspide.attach(PIN_SERVO_CUSPIDE, 500, 2400);
  servoBoca.attach(PIN_SERVO_BOCA, 500, 2400);

  configurarLED();

  mp3 = new AudioGeneratorMP3();
  salida = new AudioOutputI2SNoDAC();
  fuente = new AudioFileSourceSD();
  salida->SetOutputModeMono(true);

  if (OTAhabilitado)
    initOTA();
  
  reproducirIntroduccion();
}

void loop() {
  OTAhabilitado ? ArduinoOTA.handle() : yield();
  
  unsigned long tiempoActual = millis();

  if (mp3->isRunning()) {
    if (!mp3->loop() && yaReprodujo) {
      yaReprodujo = false;
      mp3->stop();
      fuente->close();
      Serial.println("Audio Stop");
      Serial.println("Archivo Cerrado");
      servoCuspide.write(90);
      servoBoca.write(90);
      yield();
    } else {
      moverServoCuspide();
      moverServoBoca();
    }
  } else {
    if (rubroSeleccionado == -1) {
      verificarSeleccionRubro();
    } else if (preguntaActual < preguntasPorJuego) {
      verificarRespuestaPregunta();
    } else {
      finalizarJuego();
    }
  }
}

void verificarSeleccionRubro() {
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTON_1 + i);
    unsigned long tiempoActual = millis();
    
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true;
      ultimosTiemposPulsadores[i] = tiempoActual;
      rubroSeleccionado = i;
      Serial.print("Rubro seleccionado: ");
      Serial.println(rubroSeleccionado);
      seleccionarPreguntasAleatorias();
      reproducirPregunta(preguntaActual);
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false;
    }
  }
}

void seleccionarPreguntasAleatorias() {
  for (int i = 0; i < preguntasPorJuego; i++) {
    bool preguntaUnica;
    int nuevaPregunta;
    do {
      nuevaPregunta = random(1, preguntasPorRubro + 1);
      preguntaUnica = true;
      for (int j = 0; j < i; j++) {
        if (preguntasSeleccionadas[j] == nuevaPregunta) {
          preguntaUnica = false;
          break;
        }
      }
    } while (!preguntaUnica);
    preguntasSeleccionadas[i] = nuevaPregunta;
  }
}

void verificarRespuestaPregunta() {
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTON_1 + i);
    unsigned long tiempoActual = millis();
    
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true;
      ultimosTiemposPulsadores[i] = tiempoActual;
      
      // Verificar si la respuesta es correcta (suponemos que la respuesta correcta está en un archivo)
      char rutaRespuestaCorrecta[50];
      snprintf(rutaRespuestaCorrecta, sizeof(rutaRespuestaCorrecta), "/rubro%d/pregunta%d_respuesta.txt", rubroSeleccionado + 1, preguntasSeleccionadas[preguntaActual]);
      
      File archivoRespuesta = SD.open(rutaRespuestaCorrecta);
      if (archivoRespuesta) {
        int respuestaCorrecta = archivoRespuesta.parseInt();
        archivoRespuesta.close();
        
        if (i + 1 == respuestaCorrecta) {
          respuestasCorrectas++;
          LedPWM(0, 255, 0); // Verde para respuesta correcta
          reproducirAudio("/correcta.mp3");
        } else {
          LedPWM(255, 0, 0); // Rojo para respuesta incorrecta
          reproducirAudio("/incorrecta.mp3");
        }
      } else {
        Serial.println("Error al abrir el archivo de respuesta");
      }
      
      preguntaActual++;
      if (preguntaActual < preguntasPorJuego) {
        reproducirPregunta(preguntaActual);
      }
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false;
    }
  }
}

void reproducirPregunta(int numeroPregunta) {
  char rutaPregunta[50];
  snprintf(rutaPregunta, sizeof(rutaPregunta), "/rubro%d/pregunta%d.mp3", rubroSeleccionado + 1, preguntasSeleccionadas[numeroPregunta]);
  reproducirAudio(rutaPregunta);
}

void finalizarJuego() {
  const char* archivoResultado;
  if (respuestasCorrectas == preguntasPorJuego) {
    archivoResultado = "/campeon.mp3";
    LedPWM(255, 255, 255); // Blanco para campeón
  } else if (respuestasCorrectas > preguntasPorJuego * 0.7) {
    archivoResultado = "/ganador.mp3";
    LedPWM(255, 165, 0); // Naranja para ganador
  } else {
    archivoResultado = "/perdedor.mp3";
    LedPWM(0, 0, 255); // Azul para perdedor
  }
  reproducirAudio(archivoResultado);
  
  // Reiniciar variables para un nuevo juego
  rubroSeleccionado = -1;
  preguntaActual = 0;
  respuestasCorrectas = 0;
}

void reproducirIntroduccion() {
  const char *archivoIntroduccion = "/intro.mp3";
  reproducirAudio(archivoIntroduccion);
}

void reproducirAudio(const char *ruta) {
  if (!SD.exists(ruta)) {
    Serial.println("Archivo no encontrado");
    return;
  }

  if (!fuente->open(ruta)) {
    Serial.println("Error al abrir el archivo");
    return;
  }

  yield();
  mp3->begin(fuente, salida);
  yaReprodujo = true;
  
  moverServoCuspide();
  moverServoBoca();
}

void configurarLED() {
  ledcSetup(CANAL_LEDC_0, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_1, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_2, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  
  ledcAttachPin(PIN_LED_ROJO, CANAL_LEDC_0);
  ledcAttachPin(PIN_LED_VERDE, CANAL_LEDC_1);
  ledcAttachPin(PIN_LED_AZUL, CANAL_LEDC_2);
}

void LedPWM(int rojo, int verde, int azul) {
  ledcWrite(CANAL_LEDC_0, rojo);
  ledcWrite(CANAL_LEDC_1, verde);
  ledcWrite(CANAL_LEDC_2, azul);
}

void initOTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Conexión fallida! Reiniciando...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname("ESP32-TriviaMagos");
  ArduinoOTA.begin();
}

void moverServoCuspide() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimoMovimientoCuspide >= intervaloMovimientoCuspide) {
    int posicion = random(70, 111);
    servoCuspide.write(posicion);
    ultimoMovimientoCuspide = tiempoActual;
  }
}

void moverServoBoca() {
  unsigned long tiempoActual = millis();
  if (tiempoActual - ultimoMovimientoBoca >= intervaloMovimientoBoca) {
    int posicion = random(80, 101);
    servoBoca.write(posicion);
    ultimoMovimientoBoca = tiempoActual;
  }
}
