const char *version = "1.01"; // Versión del programa 
// Adaptación para la Trivia de los 3 Magos

#include "AudioFileSourceSD.h" // Incluye la biblioteca para manejar archivos de audio desde la tarjeta SD
#include "AudioGeneratorMP3.h" // Incluye la biblioteca para generar audio en formato MP3
#include "AudioOutputI2SNoDAC.h" // Incluye la biblioteca para salida de audio I2S sin DAC
#include "FS.h" // Incluye la biblioteca del sistema de archivos
#include "SD.h" // Incluye la biblioteca para manejar la tarjeta SD
#include "SPI.h" // Incluye la biblioteca para la comunicación SPI
#include <WiFi.h> // Incluye la biblioteca para conectividad WiFi
#include <ArduinoOTA.h> // Incluye la biblioteca para actualizaciones OTA (Over-The-Air)
#include <ESP32Servo.h> // Incluye la biblioteca para controlar servomotores

bool OTAhabilitado = false; // Variable para habilitar o deshabilitar actualizaciones OTA

// Configuración de la red WiFi
const char *ssid = ""; // Nombre de la red WiFi
const char *password = ""; // Contraseña de la red WiFi

// Pines del Bus SPI para la conexión de la Tarjeta SD
#define SCK 18 // Pin de reloj para SPI
#define MISO 19 // Pin de datos de entrada para SPI
#define MOSI 23 // Pin de datos de salida para SPI
#define CS 5 // Pin de selección de chip para la tarjeta SD

// Pines para los pulsadores (ahora activos en bajo)
#define PIN_BOTON_1 4 // Pin para el primer pulsador
#define PIN_BOTON_2 15 // Pin para el segundo pulsador
#define PIN_BOTON_3 34 // Pin para el tercer pulsador
#define PIN_BOTON_4 35 // Pin para el cuarto pulsador

// Pin para el servomotor
#define PIN_SERVO_TAPA 13 // Pin para controlar el servomotor que abre/cierra la tapa

// Pines para LED RGB
#define PIN_LED_ROJO 25 // Pin para el LED rojo
#define PIN_LED_VERDE 26 // Pin para el LED verde
#define PIN_LED_AZUL 27 // Pin para el LED azul

#define CANAL_LEDC_0 0 // Canal para el LED rojo
#define CANAL_LEDC_1 1 // Canal para el LED verde
#define CANAL_LEDC_2 2 // Canal para el LED azul

#define LEDC_TIMER_8_BIT 8 // Resolución del temporizador LEDC
#define FRECUENCIA_BASE_LEDC 5000 // Frecuencia base para el control de los LEDs

// Objeto Servo
Servo servoTapa; // Crea un objeto servo para controlar la tapa
bool pulsadorPresionado = false; // Variable para verificar si un pulsador ha sido presionado

// Variables para el estado de los pulsadores y manejo del debounce
bool pulsadoresPresionados[4] = {false, false, false, false}; // Arreglo que guarda el estado de cada pulsador
unsigned long ultimosTiemposPulsadores[4] = {0, 0, 0, 0}; // Arreglo que almacena el último tiempo de pulsación de cada botón
const unsigned long debounceDelay = 120; // Tiempo de espera para evitar rebotes en los pulsadores

// Variables para el manejo del audio
AudioGeneratorMP3 *mp3; // Puntero para el generador de audio MP3
AudioFileSourceSD *fuente; // Puntero para la fuente de audio desde la tarjeta SD
AudioOutputI2SNoDAC *salida; // Puntero para la salida de audio I2S sin DAC
bool yaReprodujo = false; // Bandera que indica si ya se ha reproducido el audio de introducción

// Variables para el manejo de la lógica del cuestionario
int categoriaSeleccionada = -1; // Variable que guarda la categoría seleccionada por el usuario
const int totalCategorias = 4; // Total de categorías disponibles
const int preguntasPorCategoria = 30; // Total de preguntas por cada categoría
const int preguntasPorJuego = 5; // Total de preguntas que se jugarán en una partida
int preguntasSeleccionadas[preguntasPorJuego]; // Arreglo que guarda las preguntas seleccionadas para el juego
int preguntaActual = 0; // Índice de la pregunta actual
int respuestasCorrectas = 0; // Contador de respuestas correctas
int ordenOpciones[4]; // Arreglo que guarda el orden de las opciones de respuesta

// Variable para el tiempo límite de respuesta
const unsigned long tiempoLimiteRespuesta = 30000; // 30 segundos, configurable
unsigned long tiempoInicioPregunta; // Variable que guarda el tiempo de inicio de la pregunta

// Variables para el control del servomotor
unsigned long ultimoMovimientoTapa = 0; // Tiempo del último movimiento del servomotor
const int intervaloMovimientoTapa = 500; // Intervalo de tiempo entre movimientos del servomotor

void setup() {
  Serial.begin(115200); // Inicializa la comunicación serial a 115200 baudios
  Serial.println(version); // Imprime la versión del programa en el monitor serial

  if (!SD.begin(CS)) { // Intenta inicializar la tarjeta SD
    Serial.println("Tarjeta SD no encontrada"); // Mensaje de error si la tarjeta SD no se encuentra
    return; // Sale de la función si no se encuentra la tarjeta
  }

  randomSeed(analogRead(0)); // Inicializa la semilla para la generación de números aleatorios

  pinMode(PIN_BOTON_1, INPUT_PULLUP); // Configura el primer botón como entrada con resistencia pull-up
  pinMode(PIN_BOTON_2, INPUT_PULLUP); // Configura el segundo botón como entrada con resistencia pull-up
  pinMode(PIN_BOTON_3, INPUT_PULLUP); // Configura el tercer botón como entrada con resistencia pull-up
  pinMode(PIN_BOTON_4, INPUT_PULLUP); // Configura el cuarto botón como entrada con resistencia pull-up
  
  ESP32PWM::allocateTimer(0); // Asigna un temporizador para el control de PWM
  servoTapa.setPeriodHertz(50); // Establece la frecuencia del servomotor a 50 Hz
  servoTapa.attach(PIN_SERVO_TAPA, 500, 2400); // Conecta el servomotor al pin especificado con rango de pulso

  configurarLED(); // Llama a la función para configurar el LED

  mp3 = new AudioGeneratorMP3(); // Crea una nueva instancia del generador de audio MP3
  salida = new AudioOutputI2SNoDAC(); // Crea una nueva instancia de salida de audio I2S sin DAC
  fuente = new AudioFileSourceSD(); // Crea una nueva instancia de fuente de audio desde la tarjeta SD
  salida->SetOutputModeMono(true); // Configura la salida de audio en modo mono

  if (OTAhabilitado) // Verifica si la OTA está habilitada
    iniciarOTA(); // Llama a la función para iniciar la actualización OTA
  
  reproducirIntroduccion(); // Llama a la función para reproducir el audio de introducción
}
void loop() {
  // Verifica si la OTA está habilitada y maneja la actualización, de lo contrario, libera el procesador
  OTAhabilitado ? ArduinoOTA.handle() : yield();
  
  // Captura el tiempo actual en milisegundos
  unsigned long tiempoActual = millis();

  // Comprueba si el reproductor de MP3 está en funcionamiento
  if (mp3->isRunning()) {
    // Si el MP3 no está en bucle y ya se reprodujo, detiene la reproducción
    if (!mp3->loop() && yaReprodujo) {
      yaReprodujo = false; // Reinicia la bandera de reproducción
      mp3->stop(); // Detiene el MP3
      fuente->close(); // Cierra la fuente de audio
      Serial.println("Audio Stop"); // Imprime en el monitor serie que el audio se detuvo
      Serial.println("Archivo Cerrado"); // Imprime que el archivo se cerró
      servoTapa.write(90); // Mueve el servo a la posición 90 grados
      yield(); // Libera el procesador para otras tareas
    } else {
      moverServoTapa(); // Llama a la función para mover la tapa del servo
    }
  } else {
    // Si no hay un MP3 en ejecución, verifica la categoría seleccionada
    if (categoriaSeleccionada == -1) {
      verificarSeleccionCategoria(); // Llama a la función para verificar la selección de categoría
    } else if (preguntaActual < preguntasPorJuego) {
      // Si hay preguntas por jugar, verifica si se debe reproducir la pregunta
      if (!yaReprodujo) {
        reproducirPregunta(preguntaActual); // Reproduce la pregunta actual
      } else {
        verificarRespuestaPregunta(); // Verifica la respuesta a la pregunta actual
      }
    } else {
      finalizarJuego(); // Finaliza el juego si no hay más preguntas
    }
  }
}

void verificarSeleccionCategoria() {
  // Recorre los botones para verificar la selección de categoría
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTON_1 + i); // Lee el estado del botón
    unsigned long tiempoActual = millis(); // Captura el tiempo actual

    // Verifica si el botón fue presionado y si no ha sido registrado antes
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true; // Marca el botón como presionado
      ultimosTiemposPulsadores[i] = tiempoActual; // Actualiza el tiempo del último botón presionado
      categoriaSeleccionada = i; // Asigna la categoría seleccionada
      Serial.print("Categoría seleccionada: "); // Imprime la categoría seleccionada
      Serial.println(categoriaSeleccionada); // Muestra el número de categoría en el monitor serie
      seleccionarPreguntasAleatorias(); // Llama a la función para seleccionar preguntas aleatorias
      reproducirPregunta(preguntaActual); // Reproduce la pregunta actual
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false; // Marca el botón como no presionado
    }
  }
}
void seleccionarPreguntasAleatorias() {
  // Iterar a través de la cantidad de preguntas que se jugarán
  for (int i = 0; i < preguntasPorJuego; i++) {
    bool preguntaUnica; // Variable para verificar si la pregunta es única
    int nuevaPregunta; // Variable para almacenar la nueva pregunta aleatoria
    do {
      // Generar un número aleatorio entre 1 y el total de preguntas por categoría
      nuevaPregunta = random(1, preguntasPorCategoria + 1);
      preguntaUnica = true; // Asumir que la pregunta es única
      // Verificar si la nueva pregunta ya ha sido seleccionada
      for (int j = 0; j < i; j++) {
        if (preguntasSeleccionadas[j] == nuevaPregunta) {
          preguntaUnica = false; // No es única, cambiar la variable
          break; // Salir del bucle si se encuentra una coincidencia
        }
      }
    } while (!preguntaUnica); // Repetir hasta que se encuentre una pregunta única
    preguntasSeleccionadas[i] = nuevaPregunta; // Almacenar la pregunta seleccionada
  }
}

void verificarRespuestaPregunta() {
  unsigned long tiempoActual = millis(); // Obtener el tiempo actual
  
  // Comprobar si el tiempo límite para responder ha pasado
  if (tiempoActual - tiempoInicioPregunta >= tiempoLimiteRespuesta) {
    // Tiempo agotado, marcar como incorrecta y avanzar
    LedPWM(255, 0, 0); // Encender LED rojo para respuesta incorrecta
    reproducirAudio("/incorrecta.mp3"); // Reproducir audio de respuesta incorrecta
    avanzarSiguientePregunta(); // Avanzar a la siguiente pregunta
    return; // Salir de la función
  }

  // Verificar las respuestas de los botones
  for (int i = 0; i < 4; i++) {
    int lectura = digitalRead(PIN_BOTON_1 + i); // Leer el estado del botón
    
    // Comprobar si el botón fue presionado
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      pulsadoresPresionados[i] = true; // Marcar el botón como presionado
      ultimosTiemposPulsadores[i] = tiempoActual; // Actualizar el tiempo del último botón presionado
      
      // Verificar si la respuesta es correcta
      if (ordenOpciones[i] == 0) { // La opción 1 (índice 0) siempre es la correcta
        respuestasCorrectas++; // Incrementar el contador de respuestas correctas
        LedPWM(0, 255, 0); // Encender LED verde para respuesta correcta
        reproducirAudio("/correcta.mp3"); // Reproducir audio de respuesta correcta
      } else {
        LedPWM(255, 0, 0); // Encender LED rojo para respuesta incorrecta
        reproducirAudio("/incorrecta.mp3"); // Reproducir audio de respuesta incorrecta
      }
      
      avanzarSiguientePregunta(); // Avanzar a la siguiente pregunta
      return; // Salir de la función
    } else if (lectura == HIGH) {
      pulsadoresPresionados[i] = false; // Marcar el botón como no presionado
    }
  }
}
void avanzarSiguientePregunta() {
  preguntaActual++; // Incrementar el índice de la pregunta actual
  if (preguntaActual < preguntasPorJuego) { // Verificar si hay más preguntas disponibles
    yaReprodujo = false; // Preparar para reproducir la siguiente pregunta y opciones
  }
}

void reproducirPregunta(int numeroPregunta) {
  char rutaPregunta[50]; // Declarar un arreglo de caracteres para la ruta del audio
  // Formatear la ruta del archivo de audio de la pregunta
  snprintf(rutaPregunta, sizeof(rutaPregunta), "/categoria%d/pregunta%d.mp3", categoriaSeleccionada + 1, preguntasSeleccionadas[numeroPregunta]);
  reproducirAudio(rutaPregunta); // Llamar a la función para reproducir el audio de la pregunta
  yaReprodujo = false; // Preparar para reproducir las opciones
  tiempoInicioPregunta = millis(); // Iniciar el temporizador para la pregunta
}

void reproducirOpciones() {
  // Generar orden aleatorio de opciones
  for (int i = 0; i < 4; i++) {
    ordenOpciones[i] = i; // Asignar el índice a cada opción
  }
  // Barajar las opciones aleatoriamente
  for (int i = 3; i > 0; i--) {
    int j = random(0, i + 1); // Generar un índice aleatorio
    int temp = ordenOpciones[i]; // Almacenar el valor actual
    ordenOpciones[i] = ordenOpciones[j]; // Intercambiar los valores
    ordenOpciones[j] = temp; // Completar el intercambio
  }

  // Reproducir opciones en orden aleatorio
  for (int i = 0; i < 4; i++) {
    char rutaOpcion[50]; // Declarar un arreglo de caracteres para la ruta de la opción
    // Formatear la ruta del archivo de audio de la opción
    snprintf(rutaOpcion, sizeof(rutaOpcion), "/categoria%d/pregunta%d_opcion%d.mp3", 
             categoriaSeleccionada + 1, preguntasSeleccionadas[preguntaActual], ordenOpciones[i] + 1);
    reproducirAudio(rutaOpcion); // Llamar a la función para reproducir el audio de la opción
    while (mp3->isRunning()) { // Mientras el audio esté reproduciéndose
      if (!mp3->loop()) { // Verificar si el audio ha terminado de reproducirse
        mp3->stop(); // Detener la reproducción
        fuente->close(); // Cerrar la fuente de audio
        break; // Salir del bucle
      }
      moverServoTapa(); // Mover el servo de la tapa
    }
    delay(500); // Pequeña pausa entre opciones
  }
  yaReprodujo = true; // Indicar que se han reproducido todas las opciones
}
void finalizarJuego() {
  const char* archivoResultado; // Declaración de una variable para almacenar la ruta del archivo de resultado
  if (respuestasCorrectas == preguntasPorJuego) { // Verifica si todas las respuestas son correctas
    archivoResultado = "/campeon.mp3"; // Asigna el archivo de audio para el campeón
    LedPWM(255, 255, 255); // Blanco para campeón
  } else if (respuestasCorrectas > preguntasPorJuego * 0.7) { // Verifica si el jugador es un ganador
    archivoResultado = "/ganador.mp3"; // Asigna el archivo de audio para el ganador
    LedPWM(255, 165, 0); // Naranja para ganador
  } else { // Si no es campeón ni ganador, es perdedor
    archivoResultado = "/perdedor.mp3"; // Asigna el archivo de audio para el perdedor
    LedPWM(0, 0, 255); // Azul para perdedor
  }
  reproducirAudio(archivoResultado); // Reproduce el audio correspondiente
  
  // Reiniciar variables para un nuevo juego
  categoriaSeleccionada = -1; // Reinicia la categoría seleccionada
  preguntaActual = 0; // Reinicia la pregunta actual
  respuestasCorrectas = 0; // Reinicia el conteo de respuestas correctas
}

void reproducirIntroduccion() {
  const char *archivoIntroduccion = "/intro.mp3"; // Ruta del archivo de introducción
  reproducirAudio(archivoIntroduccion); // Reproduce el audio de introducción
}

void reproducirAudio(const char *ruta) {
  if (!SD.exists(ruta)) { // Verifica si el archivo existe en la tarjeta SD
    Serial.println("Archivo no encontrado"); // Mensaje de error si no se encuentra el archivo
    return; // Sale de la función si el archivo no existe
  }

  if (!fuente->open(ruta)) { // Intenta abrir el archivo de audio
    Serial.println("Error al abrir el archivo"); // Mensaje de error si no se puede abrir el archivo
    return; // Sale de la función si hay un error al abrir el archivo
  }

  yield(); // Permite que otras tareas se ejecuten
  mp3->begin(fuente, salida); // Inicia la reproducción del archivo de audio
  yaReprodujo = true; // Marca que ya se ha reproducido el audio
  
  moverServoTapa(); // Llama a la función para mover el servo de la tapa
}

void configurarLED() {
  ledcSetup(CANAL_LEDC_0, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT); // Configura el canal LEDC 0
  ledcSetup(CANAL_LEDC_1, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT); // Configura el canal LEDC 1
  ledcSetup(CANAL_LEDC_2, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT); // Configura el canal LEDC 2
  
  ledcAttachPin(PIN_LED_ROJO, CANAL_LEDC_0); // Asocia el pin del LED rojo al canal LEDC 0
  ledcAttachPin(PIN_LED_VERDE, CANAL_LEDC_1); // Asocia el pin del LED verde al canal LEDC 1
  ledcAttachPin(PIN_LED_AZUL, CANAL_LEDC_2); // Asocia el pin del LED azul al canal LEDC 2
}

void LedPWM(int rojo, int verde, int azul) {
  ledcWrite(CANAL_LEDC_0, rojo); // Escribe el valor del LED rojo
  ledcWrite(CANAL_LEDC_1, verde); // Escribe el valor del LED verde
  ledcWrite(CANAL_LEDC_2, azul); // Escribe el valor del LED azul
}

void iniciarOTA() {
  WiFi.mode(WIFI_STA); // Configura el modo WiFi como estación
  WiFi.begin(ssid, password); // Inicia la conexión WiFi con el SSID y la contraseña
  while (WiFi.waitForConnectResult() != WL_CONNECTED) { // Espera a que se conecte
    Serial.println("Conexión fallida! Reiniciando..."); // Mensaje de error si la conexión falla
    delay(5000); // Espera 5 segundos antes de reiniciar
    ESP.restart(); // Reinicia el ESP
  }

  ArduinoOTA.setHostname("ESP32-TriviaMagos"); // Establece el nombre del host para OTA
  ArduinoOTA.begin(); // Inicia el proceso de OTA
}

void moverServoTapa() {
  unsigned long tiempoActual = millis(); // Obtiene el tiempo actual en milisegundos
  if (tiempoActual - ultimoMovimientoTapa >= intervaloMovimientoTapa) { // Verifica si ha pasado el intervalo
    int posicion = random(70, 111); // Genera una posición aleatoria para el servo
    servoTapa.write(posicion); // Mueve el servo a la posición generada
    ultimoMovimientoTapa = tiempoActual; // Actualiza el tiempo del último movimiento
  }
}
