const char *version = "1.05"; // Versión del programa actualizada

// Inclusión de librerías necesarias
#include "AudioFileSourceSD.h" // Librería para manejar archivos de audio desde la tarjeta SD
#include "AudioGeneratorMP3.h" // Librería para generar audio en formato MP3
#include "AudioOutputI2SNoDAC.h" // Librería para salida de audio I2S sin DAC
#include "FS.h" // Librería para el sistema de archivos
#include "SD.h" // Librería para manejar la tarjeta SD
#include "SPI.h" // Librería para la comunicación SPI
#include <WiFi.h> // Librería para manejar la conexión WiFi
#include <ESPmDNS.h> // Librería para el servicio mDNS
#include <WiFiUdp.h> // Librería para comunicación UDP sobre WiFi
#include <ArduinoOTA.h> // Librería para actualizaciones OTA
#include <esp_system.h> // Librería para funciones del sistema ESP

bool OTAhabilitado = false; // Variable para habilitar o deshabilitar OTA

// Configuración de la red WiFi
const char *ssid = ""; // Nombre de la red WiFi
const char *password = ""; // Contraseña de la red WiFi

// Definición de pines para el Bus SPI de la Tarjeta SD
#define SCK 18 // Pin de reloj para SPI
#define MISO 19 // Pin de datos de entrada para SPI
#define MOSI 23 // Pin de datos de salida para SPI
#define CS 5 // Pin de selección de chip para la tarjeta SD

// Definición de pines para los pulsadores (activos en bajo)
#define PIN_BOTON_1 4 // Pin para el primer botón
#define PIN_BOTON_2 15 // Pin para el segundo botón
#define PIN_BOTON_3 35 // Pin para el tercer botón
#define PIN_BOTON_4 34 // Pin para el cuarto botón

// Definición de pin para el motor Ventilador
#define PIN_FUEGO 13 // Pin para controlar el motor del ventilador
#define CANAL_LEDC_3 3 // Canal LEDC para el control del ventilador

// Definición de pines para LED RGB
#define PIN_LED_ROJO 33 // Pin para el LED rojo
#define PIN_LED_VERDE 14 // Pin para el LED verde
#define PIN_LED_AZUL 27 // Pin para el LED azul

// Definición de canales LEDC para PWM
#define CANAL_LEDC_0 0 // Canal LEDC para el primer PWM
#define CANAL_LEDC_1 1 // Canal LEDC para el segundo PWM
#define CANAL_LEDC_2 2 // Canal LEDC para el tercer PWM

// Configuración del timer LEDC
#define LEDC_TIMER_8_BIT 8 // Resolución del timer LEDC en bits
#define FRECUENCIA_BASE_LEDC 5000 // Frecuencia base para el PWM

// Variables para el manejo de los pulsadores
bool pulsadoresPresionados[4] = {false, false, false, false}; // Estado de los pulsadores
unsigned long ultimosTiemposPulsadores[4] = {0, 0, 0, 0}; // Último tiempo de pulsación
const unsigned long debounceDelay = 120; // Tiempo de debounce para los pulsadores

// Variables para el manejo del audio
AudioGeneratorMP3 *mp3; // Generador de audio MP3
AudioFileSourceSD *fuente; // Fuente de audio (tarjeta SD)
AudioOutputI2SNoDAC *salida; // Salida de audio

// Variables para el manejo de la lógica del cuestionario
int categoriaSeleccionada = -1; // Categoría seleccionada por el jugador
const int totalCategorias = 4; // Número total de categorías
const int preguntasPorCategoria = 15; // Número de preguntas por categoría
const int preguntasPorJuego = 5; // Número de preguntas por juego
int preguntasSeleccionadas[preguntasPorJuego]; // Array para almacenar las preguntas seleccionadas
int preguntaActual = 0; // Índice de la pregunta actual
int respuestasCorrectas = 0; // Contador de respuestas correctas
int ordenOpciones[totalCategorias]; // Orden de las opciones de respuesta

// Variable para el tiempo límite de respuesta
const unsigned long tiempoLimiteRespuesta = 30000; // Tiempo límite en milisegundos
unsigned long tiempoInicioPregunta = 0; // Tiempo de inicio de la pregunta actual

// Variables para el control del motor del ventilador
unsigned long ultimoMovimientoFuego = 0; // Último tiempo de movimiento del fuego
const int intervaloMovimientoFuego = 200; // Intervalo entre movimientos del fuego
const int velocidadMaximaFuego = 144; // Velocidad máxima del fuego
const int velocidadMinimaFuego = 254; // Velocidad mínima del fuego

// Enumeración para el manejo de estados del juego
// Estado inicial donde se presenta el juego al usuario
// Estado inicial donde se presenta el juego al usuario
// Estado donde el usuario elige la categoría de preguntas
// Estado en el que se muestra la pregunta al usuario
// Estado donde se presentan las opciones de respuesta
// Estado en el que el juego espera la respuesta del usuario
// Estado que muestra el resultado de la respuesta
// Estado final que indica que el juego ha terminado
enum EstadoJuego {
  INTRODUCCION,          
  SELECCION_CATEGORIA,   
  REPRODUCCION_PREGUNTA, 
  REPRODUCCION_OPCIONES,  
  ESPERA_RESPUESTA,      
  REPRODUCCION_RESULTADO, 
  FIN_JUEGO              
};

EstadoJuego estadoActual = INTRODUCCION; // Estado actual del juego
bool reproduccionEnCurso = false; // Indica si hay una reproducción de audio en curso
String archivoAudioActual = ""; // Nombre del archivo de audio actual
bool respuestaCorrecta = false; // Indica si la respuesta dada es correcta
bool yaReprodujoFin = false; // Indica si ya analizo una vez el fin del juego

// Nuevas variables para el manejo de la reproducción de opciones
int opcionActual = 0; // Índice de la opción actual
bool reproduccionAnuncioOpcion = false; // Indica si se está reproduciendo el anuncio de opción

void setup() {
  Serial.begin(115200); // Inicialización de la comunicación serial a 115200 baudios
  Serial.println(version); // Impresión de la versión del programa en el monitor serial

  // Inicialización de la tarjeta SD
  if (!SD.begin(CS)) { // Comienza la tarjeta SD usando el pin CS
    Serial.println("Tarjeta SD no encontrada"); // Mensaje de error si la tarjeta SD no se encuentra
    return; // Sale de la función si la tarjeta SD no está disponible
  }

  randomSeed(esp_random()); // Inicialización de la semilla aleatoria usando un número aleatorio del ESP

  // Configuración de los pines de los botones como entrada con pull-up
  pinMode(PIN_BOTON_1, INPUT_PULLUP); // Configura el PIN_BOTON_1 como entrada con resistencia pull-up
  pinMode(PIN_BOTON_2, INPUT_PULLUP); // Configura el PIN_BOTON_2 como entrada con resistencia pull-up
  pinMode(PIN_BOTON_3, INPUT_PULLUP); // Configura el PIN_BOTON_3 como entrada con resistencia pull-up
  pinMode(PIN_BOTON_4, INPUT_PULLUP); // Configura el PIN_BOTON_4 como entrada con resistencia pull-up
  
  configurarLED(); // Llama a la función para configurar los pines del LED RGB
  configurarFuego(); // Llama a la función para configurar el pin del ventilador

  // Inicialización de los objetos de audio
  mp3 = new AudioGeneratorMP3(); // Crea un nuevo generador de audio MP3
  salida = new AudioOutputI2SNoDAC(); // Crea un nuevo objeto de salida de audio I2S sin DAC
  fuente = new AudioFileSourceSD(); // Crea un nuevo objeto de fuente de archivo de audio desde la tarjeta SD
  salida->SetOutputModeMono(true); // Configura la salida de audio en modo mono

  if (OTAhabilitado) // Verifica si la OTA está habilitada
    iniciarOTA(); // Llama a la función para inicializar OTA si está habilitado
  
  estadoActual = INTRODUCCION; // Establece el estado inicial del juego a INTRODUCCION
}

void loop() {
  yield(); // Permite que otras tareas se ejecuten, evitando que el programa se congele
  moverFuego(); // Actualización del movimiento del fuego

  // Máquina de estados para el manejo del juego
  switch (estadoActual) {
    case INTRODUCCION: // Estado de introducción del juego
      manejarIntroduccion(); // Llama a la función que maneja la introducción
      break;
    case SELECCION_CATEGORIA: // Estado donde el jugador selecciona la categoría
      manejarSeleccionCategoria(); // Llama a la función para manejar la selección de categoría
      break;
    case REPRODUCCION_PREGUNTA: // Estado para reproducir la pregunta
      manejarReproduccionPregunta(); // Llama a la función que maneja la reproducción de la pregunta
      break;
    case REPRODUCCION_OPCIONES: // Estado para mostrar las opciones de respuesta
      manejarReproduccionOpciones(); // Llama a la función que maneja la reproducción de opciones
      break;
    case ESPERA_RESPUESTA: // Estado donde se espera la respuesta del jugador
      manejarEsperaRespuesta(); // Llama a la función que maneja la espera de respuesta
      break;
    case REPRODUCCION_RESULTADO: // Estado para reproducir el resultado
      manejarReproduccionResultado(); // Llama a la función que maneja la reproducción del resultado
      break;
    case FIN_JUEGO: // Estado final del juego
      manejarFinJuego(); // Llama a la función que maneja el fin del juego
      break;
  }
  
  manejarReproduccionAudio(); // Manejo de la reproducción de audio
}

void manejarReproduccionAudio() {
  if (mp3->isRunning()) { // Si hay una reproducción en curso
    if (!mp3->loop()) { // Si la reproducción ha terminado
      yield(); // Permitir que otras tareas se ejecuten
      mp3->stop(); // Detener la reproducción
      fuente->close(); // Cerrar el archivo de audio
      reproduccionEnCurso = false; // Marcar que no hay reproducción en curso
      Serial.println("Audio detenido y archivo cerrado"); // Mensaje de estado
      
      // Manejo de la transición de estados después de la reproducción
      switch (estadoActual) {
        case INTRODUCCION: // Si el estado actual es INTRODUCCION
         Serial.println("Estado actual Intro pasando a Seleccion de categória"); // Mensaje de transición
          estadoActual = SELECCION_CATEGORIA; // Cambiar a SELECCION_CATEGORIA
          break;
        case REPRODUCCION_PREGUNTA: // Si el estado actual es REPRODUCCION_PREGUNTA
          Serial.println("Estado actual REPRODUCCION_PREGUNTA: pasando a REPRODUCCION_OPCIONES"); // Mensaje de transición
          estadoActual = REPRODUCCION_OPCIONES; // Cambiar a REPRODUCCION_OPCIONES
          opcionActual = 0; // Reiniciar el contador de opciones
          reproduccionAnuncioOpcion = true; // Preparar para reproducir el anuncio de opción
          break;
        case REPRODUCCION_OPCIONES: // Si el estado actual es REPRODUCCION_OPCIONES
          if (reproduccionAnuncioOpcion) { // Si se está reproduciendo el anuncio de opción
            reproduccionAnuncioOpcion = false; // Cambiar a reproducción de la opción
          } else {
            opcionActual++; // Pasar a la siguiente opción
            if (opcionActual < 4) { // Si hay más opciones disponibles
              reproduccionAnuncioOpcion = true; // Preparar para el siguiente anuncio
            } else {
              Serial.println("Estado actual REPRODUCCION_OPCIONES pasando a ESPERA_RESPUESTA:"); // Mensaje de transición
              estadoActual = ESPERA_RESPUESTA; // Cambiar a ESPERA_RESPUESTA
              tiempoInicioPregunta = millis(); // Guardar el tiempo de inicio de la pregunta
            }
          }
          break;
        case REPRODUCCION_RESULTADO: // Si el estado actual es REPRODUCCION_RESULTADO
          preguntaActual++; // Incrementar el contador de preguntas
          if (preguntaActual < preguntasPorJuego) { // Si hay más preguntas por jugar
            estadoActual = REPRODUCCION_PREGUNTA; // Cambiar a REPRODUCCION_PREGUNTA
          } else {
            estadoActual = FIN_JUEGO; // Cambiar a FIN_JUEGO si no hay más preguntas
          }
          break;
        default: // Si el estado no coincide con ninguno de los anteriores
          break; // No hacer nada
      }
    }
  } else if (!reproduccionEnCurso && !archivoAudioActual.isEmpty()) { // Si no hay reproducción y hay un archivo pendiente
    reproducirAudio(archivoAudioActual.c_str()); // Llamar a la función para reproducir el audio
    //if (fuente->open(archivoAudioActual.c_str())) { // Abrir el archivo de audio
      //mp3->begin(fuente, salida); // Iniciar la reproducción
      reproduccionEnCurso = true; // Marcar que se ha iniciado la reproducción
      //Serial.println("Iniciando reproducción de: " + archivoAudioActual); // Mensaje de inicio de reproducción
    //} else {
    //  Serial.println("Error al abrir el archivo: " + archivoAudioActual); // Mensaje de error si no se puede abrir el archivo
    //  archivoAudioActual = ""; // Limpiar la ruta del archivo
    //}
  }
}

void reproducirAudio(const char *ruta) {
  if (!SD.exists(ruta)) { // Verifica si el archivo existe en la tarjeta SD
    Serial.print("Archivo no encontrado: "); // Mensaje de error si no se encuentra el archivo
    Serial.println(ruta); // Mostrar el nombre del archivo
    return; // Sale de la función si el archivo no existe
  }

  if (!fuente->open(ruta)) { // Intenta abrir el archivo de audio
    Serial.print("Error al abrir el archivo: "); // Mensaje de error si no se puede abrir el archivo
    Serial.println(ruta); // Mostrar el nombre del archivo
    return; // Sale de la función si hay un error al abrir el archivo
  }
  Serial.print("Iniciando reproducción de: "); // Mensaje de inicio de reproducción
  Serial.println(ruta); // Mostrar el nombre del archivo que se está reproduciendo
  yield(); // Permite que otras tareas se ejecuten
  mp3->begin(fuente, salida); // Inicia la reproducción del archivo de audio
  //yaReprodujo = true; // Marca que ya se ha reproducido el audio
  archivoAudioActual = ""; // Limpiar la ruta de audio 
}

void manejarIntroduccion() {
  if (!reproduccionEnCurso && archivoAudioActual.isEmpty()) { // Si no hay reproducción y no hay archivo de audio
    archivoAudioActual = "/intro.mp3"; // Establecer el archivo de introducción
  }
}
void manejarSeleccionCategoria() {
  // Definimos los pines de los botones
  int PIN_BOTONES[4] = {PIN_BOTON_1, PIN_BOTON_2, PIN_BOTON_3, PIN_BOTON_4};
  // Guardamos el tiempo actual
  unsigned long tiempoActual = millis();
  // Variable para verificar si se ha seleccionado una categoría
  bool yaSelecciono = false;

  // Bucle que se ejecuta hasta que se seleccione una categoría
  while (!yaSelecciono) {
    // Manejo de OTA (Over-The-Air) si está habilitado
    OTAhabilitado ? ArduinoOTA.handle() : yield();
    
    // Recorremos los botones
    for (int i = 0; i < 4; i++) {
      // Leemos el estado del botón
      int lectura = digitalRead(PIN_BOTONES[i]);
      // Actualizamos el tiempo actual
      unsigned long tiempoActual = millis();

      // Verificamos si el botón fue presionado
      if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
        // Marcamos el botón como presionado
        pulsadoresPresionados[i] = true;
        // Actualizamos el tiempo de la última pulsación
        ultimosTiemposPulsadores[i] = tiempoActual;
        // Guardamos la categoría seleccionada
        categoriaSeleccionada = i;
        Serial.print("Categoría seleccionada: ");
        Serial.println(categoriaSeleccionada + 1);
        // Llamamos a la función para seleccionar preguntas aleatorias
        seleccionarPreguntasAleatorias();
        Serial.println("Estado actual manejarSeleccionCategoria pasando a REPRODUCCION_PREGUNTA:");
        // Cambiamos el estado actual
        estadoActual = REPRODUCCION_PREGUNTA;
        yaSelecciono = true; // Marcamos que ya se seleccionó
        break; // Salimos del bucle
      } else if (lectura == HIGH) {
        // Si el botón no está presionado, lo marcamos como no presionado
        pulsadoresPresionados[i] = false;
      }
    }
    yield(); // Permite que otras tareas se ejecuten
  }
}

void manejarReproduccionPregunta() {
  // Verificamos si no hay reproducción en curso y si no hay archivo de audio actual
  if (!reproduccionEnCurso && archivoAudioActual.isEmpty()) {
    // Creamos la ruta del archivo de audio de la pregunta
    char rutaPregunta[50];
    snprintf(rutaPregunta, sizeof(rutaPregunta), "/categoria%d/pregunta%d.mp3", categoriaSeleccionada + 1, preguntasSeleccionadas[preguntaActual]);
    archivoAudioActual = String(rutaPregunta); // Asignamos la ruta al archivo actual
    Serial.println("Pregunta a reproducir: " + archivoAudioActual); // Mostramos la pregunta en el monitor serie
  }
}

void manejarReproduccionOpciones() {
  // Verificamos si no hay reproducción en curso y si no hay archivo de audio actual
  if (!reproduccionEnCurso && archivoAudioActual.isEmpty()) {
    if (reproduccionAnuncioOpcion) {
      // Reproducir el anuncio de la opción
      char rutaAnuncio[50];
      snprintf(rutaAnuncio, sizeof(rutaAnuncio), "/anuncio_opcion%d.mp3", opcionActual + 1);
      archivoAudioActual = String(rutaAnuncio); // Asignamos la ruta del anuncio
      Serial.println("Asignacion de Boton: " + archivoAudioActual); // Mostramos la asignación en el monitor serie
    } else {
      // Reproducir la opción
      char rutaOpcion[50];
      snprintf(rutaOpcion, sizeof(rutaOpcion), "/categoria%d/pregunta%d_opcion%d.mp3", 
               categoriaSeleccionada + 1, preguntasSeleccionadas[preguntaActual], ordenOpciones[opcionActual] + 1);
      archivoAudioActual = String(rutaOpcion); // Asignamos la ruta de la opción
      Serial.println("Archivo de pregunta correspondiente: " + archivoAudioActual); // Mostramos la opción en el monitor serie
    }
  }
}

void manejarEsperaRespuesta() {
  // Guardamos el tiempo actual
  unsigned long tiempoActual = millis();
  // Verificamos si el tiempo límite de respuesta ha sido superado
  if (tiempoActual - tiempoInicioPregunta > tiempoLimiteRespuesta) {
    // Tiempo agotado, pasar a la siguiente pregunta
    estadoActual = REPRODUCCION_RESULTADO;
    respuestaCorrecta = false; // Marcamos que la respuesta no fue correcta
    return; // Salimos de la función
  }

  // Definimos los pines de los botones
  int PIN_BOTONES[4] = {PIN_BOTON_1, PIN_BOTON_2, PIN_BOTON_3, PIN_BOTON_4};
  // Recorremos los botones
  for (int i = 0; i < 4; i++) {
    // Leemos el estado del botón
    int lectura = digitalRead(PIN_BOTONES[i]);
    // Verificamos si el botón fue presionado
    if (lectura == LOW && !pulsadoresPresionados[i] && (tiempoActual - ultimosTiemposPulsadores[i] > debounceDelay)) {
      // Marcamos el botón como presionado
      pulsadoresPresionados[i] = true;
      // Actualizamos el tiempo de la última pulsación
      ultimosTiemposPulsadores[i] = tiempoActual;
      
      // Verificar si la respuesta es correcta
      respuestaCorrecta = ((ordenOpciones[i] + 1) == 1); // Asumiendo que la primera opción es siempre la correcta  
      if (respuestaCorrecta) {
        respuestasCorrectas++; // Incrementamos el contador de respuestas correctas
      }   
      estadoActual = REPRODUCCION_RESULTADO; // Cambiamos el estado a resultado
      mezclarOrdenOpciones(); // Mezclamos las opciones para la siguiente pregunta
      break; // Salimos del bucle
    } else if (lectura == HIGH) {
      // Si el botón no está presionado, lo marcamos como no presionado
      pulsadoresPresionados[i] = false;
    }
  }
}
void manejarReproduccionResultado() {
  // Verifica si no hay reproducción en curso y si no hay archivo de audio actual
  if (!reproduccionEnCurso && archivoAudioActual.isEmpty()) {
    // Asigna el archivo de audio según si la respuesta fue correcta o incorrecta
    archivoAudioActual = respuestaCorrecta ? "/correcta.mp3" : "/incorrecta.mp3";
  }
}

void manejarFinJuego() {
  // Verifica si no hay reproducción en curso, no hay archivo de audio actual y no se ha reproducido el fin
  if (!reproduccionEnCurso && archivoAudioActual.isEmpty() && !yaReprodujoFin) {
    // Si todas las respuestas son correctas
    if (respuestasCorrectas == preguntasPorJuego) {
      archivoAudioActual = "/campeon.mp3"; // Asigna el audio de campeón
      LedPWM(255, 255, 255); // LED blanco para campeón     
    } 
    // Si más del 70% de las respuestas son correctas
    else if (respuestasCorrectas > preguntasPorJuego * 0.7) {
      archivoAudioActual = "/ganador.mp3"; // Asigna el audio de ganador
      LedPWM(255, 165, 0); // LED naranja para ganador      
    } 
    // Si menos del 70% de las respuestas son correctas
    else {
      archivoAudioActual = "/perdedor.mp3"; // Asigna el audio de perdedor
      LedPWM(0, 0, 255); // LED azul para perdedor    
    }
    yaReprodujoFin = true; // Marca que ya se reprodujo el fin
  } 
  // Si no hay reproducción en curso
  else if (!reproduccionEnCurso) {
    delay(30000); // Espera 30 segundos
    reiniciarJuego(); // Reinicia el juego
  }
}

void reiniciarJuego() {
  categoriaSeleccionada = -1; // Reinicia la categoría seleccionada
  preguntaActual = 0; // Reinicia el contador de preguntas
  respuestasCorrectas = 0; // Reinicia el contador de respuestas correctas
  estadoActual = INTRODUCCION; // Vuelve al estado de introducción
  LedPWM(0, 0, 0); // Apaga el LED
  yaReprodujoFin = false; // Marca que no se ha reproducido el fin
}

void seleccionarPreguntasAleatorias() {
  // Selecciona preguntas aleatorias para el juego
  for (int i = 0; i < preguntasPorJuego; i++) {
    bool preguntaUnica; // Variable para verificar si la pregunta es única
    int nuevaPregunta; // Variable para almacenar la nueva pregunta
    do {
      nuevaPregunta = esp_random() % preguntasPorCategoria + 1; // Genera una nueva pregunta aleatoria
      preguntaUnica = true; // Asume que la pregunta es única
      // Verifica si la nueva pregunta ya fue seleccionada
      for (int j = 0; j < i; j++) {
        if (preguntasSeleccionadas[j] == nuevaPregunta) {
          preguntaUnica = false; // Marca que la pregunta no es única
          break; // Sale del bucle
        }
      }
    } while (!preguntaUnica); // Repite hasta que se encuentre una pregunta única
    preguntasSeleccionadas[i] = nuevaPregunta; // Almacena la pregunta seleccionada
  }
  mezclarOrdenOpciones(); // Mezcla el orden de las opciones
}

void mezclarOrdenOpciones() {
  // Mezcla el orden de las opciones
  for (int i = 0; i < totalCategorias; i++) {
    ordenOpciones[i] = i; // Inicializa el orden de opciones
  }
  // Mezcla las opciones usando el algoritmo de Fisher-Yates
  for (int i = totalCategorias - 1; i > 0; i--) {
    int j = esp_random() % (i + 1); // Selecciona un índice aleatorio
    int temp = ordenOpciones[i]; // Almacena el valor actual
    ordenOpciones[i] = ordenOpciones[j]; // Intercambia los valores
    ordenOpciones[j] = temp; // Completa el intercambio
  }
  return;  
}

void configurarLED() {
  // Configura los canales de LED
  ledcSetup(CANAL_LEDC_0, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_1, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcSetup(CANAL_LEDC_2, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  
  // Asocia los pines de los LEDs a los canales
  ledcAttachPin(PIN_LED_ROJO, CANAL_LEDC_0);
  ledcAttachPin(PIN_LED_VERDE, CANAL_LEDC_1);
  ledcAttachPin(PIN_LED_AZUL, CANAL_LEDC_2);
}

void configurarFuego() {
  // Configura el canal para el fuego
  ledcSetup(CANAL_LEDC_3, FRECUENCIA_BASE_LEDC, LEDC_TIMER_8_BIT);
  ledcAttachPin(PIN_FUEGO, CANAL_LEDC_3); // Asocia el pin del fuego al canal
}

void LedPWM(int rojo, int verde, int azul) {
  // Escribe los valores de PWM para cada color
  ledcWrite(CANAL_LEDC_0, rojo);
  ledcWrite(CANAL_LEDC_1, verde);
  ledcWrite(CANAL_LEDC_2, azul);
}

void moverFuego() {
  unsigned long tiempoActual = millis(); // Obtiene el tiempo actual
  // Verifica si ha pasado el intervalo para mover el fuego
  if (tiempoActual - ultimoMovimientoFuego >= intervaloMovimientoFuego) {
    ultimoMovimientoFuego = tiempoActual; // Actualiza el último movimiento
    int velocidadFuego = random(velocidadMinimaFuego, velocidadMaximaFuego); // Genera una velocidad aleatoria
    ledcWrite(CANAL_LEDC_3, velocidadFuego); // Escribe la velocidad en el canal del fuego
  }
}

void iniciarOTA() {
  WiFi.mode(WIFI_STA); // Configura el modo WiFi
  WiFi.begin(ssid, password); // Inicia la conexión WiFi
  // Espera hasta que se conecte
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Conexión fallida! Reiniciando..."); // Mensaje de error
    delay(10000); // Espera 10 segundos
    ESP.restart(); // Reinicia el ESP (comentado)
  }

  ArduinoOTA.setHostname("ESP32-Trivia"); // Establece el nombre del host para OTA
  ArduinoOTA
    .onStart([]() { // Callback al iniciar la actualización
      String type; // Variable para el tipo de actualización
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch"; // Si es un sketch
      else // U_SPIFFS
        type = "filesystem"; // Si es el sistema de archivos

      // NOTE: si se actualiza SPIFFS, este sería el lugar para desmontar SPIFFS usando SPIFFS.end()
      Serial.println("Start updating " + type); // Mensaje de inicio de actualización
    })
    .onEnd([]() { // Callback al finalizar la actualización
      Serial.println("\nEnd"); // Mensaje de fin
    })
    .onProgress([](unsigned int progress, unsigned int total) { // Callback para el progreso
      Serial.printf("Progress: %u%%\r", (progress / (total / 100))); // Muestra el progreso
    })
    .onError([](ota_error_t error) { // Callback para errores
      Serial.printf("Error[%u]: ", error); // Muestra el error
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed"); // Error de autenticación
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed"); // Error al iniciar
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed"); // Error de conexión
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed"); // Error de recepción
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); // Error al finalizar
    });;
  
  ArduinoOTA.begin(); // Inicia el proceso OTA
  Serial.println("OTA Listo"); // Mensaje de que OTA está listo
  Serial.print("Dirección IP: "); // Muestra la dirección IP
  Serial.println(WiFi.localIP()); // Imprime la dirección IP local
}
