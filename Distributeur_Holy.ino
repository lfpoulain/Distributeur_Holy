#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

#include <AnimatedGIF.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <CST816S.h>
#include <ESP32Servo.h>
#include "Font28.h"

#include "logo_opti.h"
#include "prepa_opti.h"

#include <L298NX2.h>  // Bibliothèque pour contrôler le module L298N

// Dimensions de l'écran TFT
#define DISPLAY_WIDTH  tft.width()
#define DISPLAY_HEIGHT tft.height()
#define BUFFER_SIZE 256  // Taille optimale pour le buffer

uint16_t usTemp[1][BUFFER_SIZE];  // Buffer pour l'affichage des GIF
bool     dmaBuf = 0;

// Images GIF pour l'écran d'accueil et de préparation
#define GIF_IMAGE logo_opti
#define PREPA_GIF prepa_opti

// Informations de connexion Wi-Fi
const char* ssid = "Votre_SSID";         // Remplacez par votre SSID
const char* password = "Votre_Mot_de_Passe";  // Remplacez par votre mot de passe Wi-Fi

// Configuration du serveur web
WebServer server(80);
const char* host = "esp32";

// Pages HTML pour l'interface de mise à jour
const char* loginIndex =
  "<form name='loginForm'>"
  "<table width='20%' bgcolor='A09F9F' align='center'>"
  "<tr>"
  "<td colspan=2>"
  "<center><font size=4><b>ESP32 Login Page</b></font></center>"
  "<br>"
  "</td>"
  "<br>"
  "<br>"
  "</tr>"
  "<td>Username:</td>"
  "<td><input type='text' size=25 name='userid'><br></td>"
  "</tr>"
  "<br>"
  "<br>"
  "<tr>"
  "<td>Password:</td>"
  "<td><input type='Password' size=25 name='pwd'><br></td>"
  "<br>"
  "<br>"
  "</tr>"
  "<tr>"
  "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
  "</tr>"
  "</table>"
  "</form>"
  "<script>"
  "function check(form)"
  "{"
  "if(form.userid.value=='admin' && form.pwd.value=='admin')"
  "{"
  "window.open('/serverIndex')"
  "}"
  "else"
  "{"
  " alert('Error Password or Username')"
  "}"
  "}"
  "</script>";

const char* serverIndex =
  "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
  "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
  "<input type='file' name='update'>"
  "<input type='submit' value='Update'>"
  "</form>"
  "<div id='prg'>progress: 0%</div>"
  "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
  "},"
  "error: function (a, b, c) {"
  "}"
  "});"
  "});"
  "</script>";


// Initialisation de l'écran TFT
TFT_eSPI tft = TFT_eSPI();

// Initialisation du capteur tactile
CST816S touch(6, 7, 13, 5);

// Gestion des GIF animés
AnimatedGIF gif;

// Servo-moteur pour la distribution de poudre
Servo servo;

// Positions du servo pour les différents goûts
const int POSITION_NEUTRE = 90;
const int POSITION_GOUT1 = 0;
const int POSITION_GOUT2 = 180;

// Définition du pin pour le servo-moteur
const int servoPin = 18;

// Définition des pins pour le module L298N
// Contrôle de la valve (Moteur A)
const unsigned int EN_A = 17;
const unsigned int IN1_A = 33;
const unsigned int IN2_A = 22;  // Non utilisé, maintenu à LOW

// Contrôle de l'agitateur (Moteur B)
const unsigned int EN_B = 16;
const unsigned int IN1_B = 21;
const unsigned int IN2_B = 23;  // Non utilisé, maintenu à LOW

// Initialisation des moteurs L298N
L298NX2 motors(EN_A, IN1_A, IN2_A, EN_B, IN1_B, IN2_B);

// Variables pour la gestion du temps
unsigned long lastTouchTime = 0;
unsigned long touchStartTime = 0;
bool touchHeld = false;
unsigned long waterStartTime = 0;
unsigned long stirStartTime = 0;
unsigned long servoActionStartTime = 0;
bool returningToHome = false;

// États de la préparation de la boisson
enum PreparationState {
  IDLE,
  ADDING_WATER,
  INITIAL_STIR,
  MOVING_TO_FLAVOR,
  WAITING_AT_FLAVOR,
  RETURNING_TO_NEUTRAL,
  FINAL_STIR,
  DISPENSING_UNLIMITED_WATER,
  COMPLETED,
  MAINTENANCE_MODE
};
PreparationState preparationState = IDLE;

// Options sélectionnées par l'utilisateur
int selectedDrink = 0;   // 1 ou 2 pour le goût, ou 3 pour "Eau"
int selectedVolume = 0;  // 500, 700 ml, ou 999 pour "Illimité"

// Délais et temporisations (modifiable si besoin)
const unsigned long drinkSelectionTimeout = 5000;  // Temps pour sélectionner la boisson
const unsigned long volumeSelectionTimeout = 5000; // Temps pour sélectionner le volume
const unsigned long debounceDelay = 500;           // Délai anti-rebond pour le tactile
const unsigned long maintenanceHoldTime = 3000;    // Temps pour activer le mode maintenance

// Durées spécifiques aux étapes de préparation (modifiable si besoin)
const unsigned long valveDuration500ml = 12000;    // Durée pour verser 500 ml d'eau
const unsigned long valveDuration700ml = 18000;    // Durée pour verser 700 ml d'eau
const unsigned long stirDuration = 5000;           // Durée initiale d'agitation
const unsigned long servoWaitTime = 1500;          // Temps d'attente après mouvement du servo
const unsigned long totalStirDuration = 10000;     // Durée totale d'agitation

void setup() {
  Serial.begin(115200);

  // Initialisation de l'écran TFT
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  // Initialisation du gestionnaire de GIF
  gif.begin(BIG_ENDIAN_PIXELS);

  // Initialisation du capteur tactile
  touch.begin();

  touchHeld = false;
  touchStartTime = 0;
  lastTouchTime = 0;

  // Allocation des timers pour le servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servo.setPeriodHertz(50);

  // Configuration des pins L298N
  pinMode(IN2_A, OUTPUT);
  digitalWrite(IN2_A, LOW);

  pinMode(IN2_B, OUTPUT);
  digitalWrite(IN2_B, LOW);

  // Initialisation des moteurs L298N
  motors.setSpeedA(255);  // Valve à vitesse maximale
  motors.setSpeedB(180);  // Agitateur à vitesse moyenne
  motors.stopA();
  motors.stopB();

  // Configuration du Wi-Fi
  WiFi.begin(ssid, password);
  Serial.println("\nConnexion au Wi-Fi en cours...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnecté au réseau Wi-Fi.");
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());

  // Configuration de mDNS
  if (!MDNS.begin(host)) {
    Serial.println("Erreur lors de la configuration de mDNS.");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("Service mDNS démarré.");

  // Configuration du serveur web
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  server.on(
    "/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "Échec" : "Succès");
      ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Mise à jour en cours : %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  // Démarrage de la mise à jour
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Écriture du firmware
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          Serial.printf("Mise à jour réussie : %u octets\nRedémarrage...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    });

  server.begin();
  Serial.println("Serveur HTTP démarré.");
}

void loop() {
  // Gestion des clients du serveur web
  server.handleClient();

  // Affichage du GIF de l'écran d'accueil
  gif.open((uint8_t*)GIF_IMAGE, sizeof(GIF_IMAGE), GIFDraw);
  tft.startWrite();
  Serial.println("Affichage du GIF d'accueil");
  returningToHome = false;

  while (gif.playFrame(true, NULL)) {
    yield();

    if (touch.available()) {
      if (!touchHeld) {
        touchStartTime = millis();
        touchHeld = true;
      }

      // Vérifier si le doigt est maintenu pendant 3 secondes pour le mode maintenance
      if (millis() - touchStartTime >= maintenanceHoldTime) {
        Serial.println("Entrée en mode maintenance.");
        gif.close();
        tft.endWrite();
        preparationState = MAINTENANCE_MODE;
        maintenanceMode();
        return;
      }
    } else {
      if (touchHeld && (millis() - lastTouchTime > debounceDelay)) {
        lastTouchTime = millis();
        touchHeld = false;
        if (returningToHome) {
          Serial.println("Ignorer l'événement tactile car retour à l'accueil en cours.");
          continue;
        }
        Serial.println("Touché détecté, arrêt du GIF et affichage du menu de sélection.");
        gif.close();
        tft.endWrite();
        displayDrinkSelection();
        return;
      }
    }
  }
}

void displayDrinkSelection() {
  // Affichage des options de sélection de boisson
  tft.fillScreen(TFT_BLACK);
  drawDrinkOption(0, 0, 240, 80, tft.color565(255, 155, 226), "Gout 1", 70, 20);
  drawDrinkOption(0, 80, 240, 80, tft.color565(155, 250, 198), "Gout 2", 70, 100);
  drawDrinkOption(0, 160, 240, 80, tft.color565(100, 149, 237), "Eau", 90, 180);

  unsigned long startTime = millis();
  while (millis() - startTime < drinkSelectionTimeout) {
    if (touch.available() && (millis() - lastTouchTime > debounceDelay)) {
      lastTouchTime = millis();
      int touchX = touch.data.x;  // Coordonnée Y du toucher

      if (touchX < 80) {
        selectedDrink = 1;
      } else if (touchX < 160) {
        selectedDrink = 2;
      } else {
        selectedDrink = 3;  // Eau
      }
      Serial.printf("Option sélectionnée : %d\n", selectedDrink);
      selectVolume();
      return;
    }
    yield();
  }

  Serial.println("Temps écoulé pour la sélection du goût, retour à l'accueil.");
  returningToHome = true;
}

void selectVolume() {
  // Affichage des options de sélection du volume
  tft.fillScreen(TFT_BLACK);
  drawDrinkOption(0, 0, 240, 80, tft.color565(255, 41, 223), "500 ml", 70, 20);
  drawDrinkOption(0, 80, 240, 80, tft.color565(41, 255, 137), "700 ml", 70, 100);
  drawDrinkOption(0, 160, 240, 80, tft.color565(255, 215, 0), "Illimité", 60, 180);

  Serial.println("Affichage de l'écran de sélection du volume.");

  unsigned long startTime = millis();
  while (millis() - startTime < volumeSelectionTimeout) {
    if (touch.available() && (millis() - lastTouchTime > debounceDelay)) {
      lastTouchTime = millis();
      int touchX = touch.data.x;  // Coordonnée Y du toucher

      if (touchX < 80) {
        selectedVolume = 500;
      } else if (touchX < 160) {
        selectedVolume = 700;
      } else {
        selectedVolume = 999;  // Illimité
      }
      Serial.printf("Volume sélectionné : %d\n", selectedVolume);
      startPreparation();
      return;
    }
    yield();
  }

  Serial.println("Temps écoulé pour la sélection du volume, retour à l'accueil.");
  returningToHome = true;
}

void startPreparation() {
  Serial.println("Début de la préparation de la boisson...");

  // Affichage du GIF de préparation
  gif.open((uint8_t*)PREPA_GIF, sizeof(PREPA_GIF), GIFDraw);
  tft.startWrite();

  // Activation du servo-moteur
  servo.attach(servoPin, 500, 2500);
  servo.write(POSITION_NEUTRE);

  // Initialisation de l'état de préparation
  preparationState = ADDING_WATER;
  waterStartTime = millis();

  // Démarrage du versement de l'eau
  motors.forwardA();  // Démarre la valve

  while (preparationState != COMPLETED) {
    updatePreparation();
    gif.playFrame(true, NULL);
    yield();
  }

  // S'assurer que le servo est en position neutre à la fin
  servo.write(POSITION_NEUTRE);
  delay(1500);  // Attendre que le servo atteigne sa position
  servo.detach();

  // Réinitialiser les variables tactiles
  touchHeld = false;
  lastTouchTime = millis();  // Mettre à jour le temps pour éviter les faux contacts
  touchStartTime = 0;        // Réinitialiser le temps de début du toucher

  returningToHome = true;
  delay(200);
}

void updatePreparation() {
  unsigned long currentTime = millis();

  switch (preparationState) {
    case ADDING_WATER:
      if (selectedVolume == 999) {  // Illimité
        preparationState = DISPENSING_UNLIMITED_WATER;
        Serial.println("Distribution d'eau illimitée.");
        break;
      }
      // Vérifier si le temps de versement de l'eau est écoulé
      if (currentTime - waterStartTime >= (selectedVolume == 500 ? valveDuration500ml : valveDuration700ml)) {
        // Arrêter le versement de l'eau
        motors.stopA();  // Arrête la valve

        // Si seulement de l'eau est sélectionnée, terminer la préparation
        if (selectedDrink == 3) {
          preparationState = COMPLETED;
          Serial.println("Distribution d'eau terminée.");
        } else {
          // Démarrer l'agitation initiale
          motors.forwardB();  // Démarre le moteur d'agitation
          stirStartTime = currentTime;

          // Passer à l'état d'agitation initiale
          preparationState = INITIAL_STIR;
          Serial.println("Eau ajoutée. Démarrage de l'agitation initiale.");
        }
      }
      break;

    case INITIAL_STIR:
      // Vérifier si la durée d'agitation initiale est écoulée
      if (currentTime - stirStartTime >= stirDuration) {
        // Passer à l'état de déplacement vers le goût
        preparationState = MOVING_TO_FLAVOR;
        servo.write((selectedDrink == 1) ? POSITION_GOUT1 : POSITION_GOUT2);
        servoActionStartTime = currentTime;
        Serial.println("Agitation initiale terminée. Déplacement du servo vers le goût.");
      }
      break;

    case MOVING_TO_FLAVOR:
      // Attendre après avoir déplacé le servo
      if (currentTime - servoActionStartTime >= servoWaitTime) {
        // Passer à l'état d'attente au goût
        preparationState = WAITING_AT_FLAVOR;
        servoActionStartTime = currentTime;
        Serial.println("Servo en position du goût. Attente de 1,5 sec.");
      }
      break;

    case WAITING_AT_FLAVOR:
      // Attendre en position du goût
      if (currentTime - servoActionStartTime >= servoWaitTime) {
        // Retourner en position neutre
        servo.write(POSITION_NEUTRE);
        servoActionStartTime = currentTime;
        preparationState = RETURNING_TO_NEUTRAL;
        Serial.println("Retour du servo en position neutre.");
      }
      break;

    case RETURNING_TO_NEUTRAL:
      // Attendre après être revenu en position neutre
      if (currentTime - servoActionStartTime >= servoWaitTime) {
        // Passer à l'agitation finale
        preparationState = FINAL_STIR;
        Serial.println("Servo en position neutre. Démarrage de l'agitation finale.");
      }
      break;

    case FINAL_STIR:
      // Continuer l'agitation pendant la durée totale
      if (currentTime - stirStartTime >= stirDuration + totalStirDuration) {
        // Arrêter l'agitation
        motors.stopB();  // Arrête le moteur d'agitation

        // Préparation terminée
        preparationState = COMPLETED;
        Serial.println("Agitation finale terminée. Boisson prête.");
      }
      break;

    case DISPENSING_UNLIMITED_WATER:
      // Vérifier si l'utilisateur a relâché le bouton pour arrêter l'eau
      if (!touch.available()) {
        motors.stopA();  // Arrête la valve
        preparationState = COMPLETED;
        Serial.println("Distribution d'eau illimitée terminée.");
      }
      break;

    case COMPLETED:
      servo.attach(servoPin, 500, 2500);
      servo.write(POSITION_NEUTRE);
      delay(1500);  // Attendre que le servo atteigne sa position
      servo.detach();
      break;

    default:
      break;
  }
}

void maintenanceMode() {
  Serial.println("Mode maintenance activé.");
  servo.attach(servoPin, 500, 2500);

  for (int i = 1; i <= 5; i++) {
    int angle = i * 36;  // Incréments de 36 degrés
    servo.write(angle);
    delay(500);      // Attendre 0,5 seconde
    servo.write(0);  // Retour à 0 degré
    delay(500);
  }

  // Revenir en position neutre
  servo.write(POSITION_NEUTRE);
  delay(1500);
  servo.detach();
  preparationState = COMPLETED;
  returningToHome = true;
}

void drawDrinkOption(int x, int y, int w, int h, uint16_t color, const char* text, int textX, int textY) {
  // Dessiner un rectangle pour l'option
  tft.fillRect(x, y, w, h, color);

  // Définir la couleur du texte et du fond
  tft.setTextColor(TFT_WHITE, color);

  // Charger la police personnalisée
  tft.loadFont(Font28);

  // Positionner le curseur et afficher le texte
  tft.setCursor(textX, textY);
  tft.println(text);

  // Décharger la police personnalisée
  tft.unloadFont();
}

void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette;
  int x, y, iWidth, iCount;

  // Vérification des limites de l'affichage et recadrage si nécessaire
  iWidth = pDraw->iWidth;
  if (iWidth + pDraw->iX > DISPLAY_WIDTH)
    iWidth = DISPLAY_WIDTH - pDraw->iX;
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y; // Ligne actuelle
  if (y >= DISPLAY_HEIGHT || pDraw->iX >= DISPLAY_WIDTH || iWidth < 1)
    return;

  // Gestion de l'effacement de l'ancienne image
  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) { // Restaurer la couleur d'arrière-plan
    for (x = 0; x < iWidth; x++) {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }

  // Application des nouveaux pixels à l'image principale
  if (pDraw->ucHasTransparency) { // Si la transparence est utilisée
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    pEnd = s + iWidth;
    x = 0;
    iCount = 0; // Compte des pixels non transparents
    while (x < iWidth) {
      c = ucTransparent - 1;
      d = &usTemp[0][0];
      while (c != ucTransparent && s < pEnd && iCount < BUFFER_SIZE) {
        c = *s++;
        if (c == ucTransparent) { // Si transparent, retour en arrière
          s--;
        } else { // Opaque
          *d++ = usPalette[c];
          iCount++;
        }
      }
      if (iCount) { // S'il y a des pixels opaques
        tft.setAddrWindow(pDraw->iX + x, y, iCount, 1);
        tft.pushPixels(usTemp[0], iCount);
        x += iCount;
        iCount = 0;
      }
      // Recherche d'une série de pixels transparents
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
          x++;
        else
          s--;
      }
    }
  } else {
    s = pDraw->pPixels;

    // Traduction des pixels 8 bits en RGB565
    if (iWidth <= BUFFER_SIZE)
      for (iCount = 0; iCount < iWidth; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];
    else
      for (iCount = 0; iCount < BUFFER_SIZE; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];

    tft.setAddrWindow(pDraw->iX, y, iWidth, 1);
    tft.pushPixels(&usTemp[0][0], iCount);

    iWidth -= iCount;
    // Boucle si le buffer de pixels est plus petit que la largeur
    while (iWidth > 0) {
      if (iWidth <= BUFFER_SIZE)
        for (iCount = 0; iCount < iWidth; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];
      else
        for (iCount = 0; iCount < BUFFER_SIZE; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];

      tft.pushPixels(&usTemp[0][0], iCount);
      iWidth -= iCount;
    }
  }
}
