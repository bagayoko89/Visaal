#include "HX711.h"
#include "WiFi.h"
#include <MQTT.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>


// attribution des broches de la carte------------------------------------------------------

const int rincage = 16;
const int aspiration = 17;
const int sck = 18;
const int collecteur = 21;
const int gaugeeau = 39;  //34  // les pins utilisés sur la carte
const int sonde = 36;
const int gaugeurine = 34;  //39
const int alerte = 32;
const int alerteWifi = 33;
char soft_ap_ssid[16];


float facteurdecalibrationjaugeurine = 409; //662.290;  //408.138;//796.478;//844.114;  //632.88
float facteurdecalibrationjaugeeau = 442.5; //451.568;    //785.150;    //659.35
float seuilreservoirurine = 1425.00;
float reservoirurineplein = 1500.00;
float rateseuilurine = (seuilreservoirurine / reservoirurineplein) * 100;
float seuilreservoireau = 100.00;
float reservoireauplein = 1500;
float rateseuileau = (seuilreservoireau / reservoireauplein) * 100;
float niveaureservoirurinecourant = 0.00;  // les variables globales
float niveaureservoirurineprecedent = 0.00;
float rate1 = (niveaureservoirurinecourant / reservoirurineplein) * 100;
float niveaureservoireau = 0.00;
float niveaureservoireaucourant = 0.00;
float rate2 = (niveaureservoireaucourant / reservoireauplein) * 100;
float volumeinst = 0.00;
float volume = 0.00;
unsigned long dernierTempsEnvoie = 0;
bool alerteStateUrine = false;
bool alerteStateEau = false;
int etatcollecteur;
bool commandeExecutee = false;
bool etataspiration=false;
bool etat_envoie=false;

HX711 scale_urine;
HX711 scale_eau;
// identifiant wifi
const char ssid[] = "";
const char pass[] = "";

WiFiClient net;
MQTTClient client;

// connexion au wifi

void connect() {
  WiFiManager wifiManager;
  wifiManager.autoConnect("connexion_visaal");
  
  Serial.print("\nconnecting...");
  while (!client.connect("arduino", "public", "public")) {
    Serial.print(".");
    delay(1000);
  }
  
  Serial.println("\nconnected!");

  client.subscribe("/visaal/poche-eau");
  // client.unsubscribe("/hello");
}

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(9600);

  

    //------------------------------------------ définitition des E/S
  pinMode(sonde, INPUT);
  pinMode(collecteur, INPUT);
  pinMode(aspiration, OUTPUT);  // les Entrées et les sorties
  digitalWrite(aspiration, LOW);
  pinMode(rincage, OUTPUT);
  digitalWrite(rincage, LOW);
  pinMode(alerte, OUTPUT);
  pinMode(alerteWifi, OUTPUT);


    //Calibration des jauges et tarages.-------------------------------------------

  scale_urine.begin(gaugeurine, sck);
  scale_urine.set_scale();
  //scale_urine.tare();

  scale_eau.begin(gaugeeau, sck);  //  les Gauges
  scale_eau.set_scale();
  //scale_eau.tare();


  // management du wifi

  WiFiManager wifiManager;
  wifiManager.autoConnect("connexion_visaal");
  client.begin("broker.hivemq.com", net);
  client.onMessage(messageReceived);

  connect();
 /* if (!client.connected()) {
    connect();
  }

  // Publie l'adresse IP sur le topic MQTT
  client.publish("/hello", soft_ap_ssid);*/
  

  //client.publish("/hello", sid);

}

void loop() {
  // put your main code here, to run repeatedly:


  // verifier connexion au wifi

  client.loop();
  delay(10);  // <- fixes some issues with WiFi stability
 // J'ai pris client connect ici
  if (!client.connected()) {
    connect();
  }


  Serial.println("\ndébut de programmes\n");


  

  scale_urine.set_scale(facteurdecalibrationjaugeurine); // étalonnage 
  float niveaureservoirurineprecedent = scale_urine.get_units()-481; 
  Serial.print(" niveau reservoire urine avant miction : ");
  Serial.println(niveaureservoirurineprecedent);

  etatcollecteur = digitalRead(collecteur);
  unsigned long currenttemps=millis();

  if (etatcollecteur == 0) {
    if (!commandeExecutee) {
      
      // Exécuter la commande pendant une durée déterminée
      unsigned long startTime = millis(); // Temps de début
      unsigned long duration = 15000; // Durée en millisecondes pour la commande (15 secondes)
      
      while ((millis() - startTime < duration || (scale_urine.get_units()-481)>niveaureservoirurineprecedent) && (scale_urine.get_units()-481)<seuilreservoirurine && etatcollecteur == 0 && (scale_eau.get_units()+236)>=seuilreservoireau) {
        digitalWrite(aspiration, HIGH);
        delay(5000);
        
        if((scale_urine.get_units()-481)>niveaureservoirurineprecedent+10){
          volume=(scale_urine.get_units()-481)-niveaureservoirurineprecedent;
          Serial.print(" la quantité urineé est : ");
          Serial.println(volume);
          commandeExecutee = true;
        }
        if (digitalRead(collecteur) != 0) {
          break; // Sortir de la boucle si etatcollecteur est différent de 0
        }
        niveaureservoirurineprecedent=scale_urine.get_units()-481;
      }
      
      startTime = millis();
      digitalWrite(aspiration, LOW);
      
      //commandeExecutee = true; // Marquer la commande comme exécutée
      if((scale_urine.get_units()-481)>seuilreservoirurine){
        etataspiration=true;
      }
    }
  }
  else {
    bool ancienneCommandeExecutee = commandeExecutee;
    //digitalWrite(aspiration, LOW); 
    commandeExecutee = false;

    scale_eau.set_scale(facteurdecalibrationjaugeeau);
    niveaureservoireau = scale_eau.get_units()+236;
    Serial.print(" niveau reservoire eau avant rincage : ");
    Serial.println(niveaureservoireau);

    while (ancienneCommandeExecutee && (scale_eau.get_units()+236)>=seuilreservoireau && (scale_urine.get_units()-481)<seuilreservoirurine ) {
      digitalWrite(rincage, HIGH);
      delay(3 * 1000);
      digitalWrite(rincage, LOW);
      delay(500);
      digitalWrite(aspiration, HIGH);
      delay(7 * 1000);
      digitalWrite(aspiration, LOW);
      ancienneCommandeExecutee=false;
      etat_envoie=true;
      dernierTempsEnvoie = millis();
      niveaureservoirurineprecedent=(scale_urine.get_units()-481);
      //niveaureservoireau=scale_eau.get_units();
    }
  
  }

  scale_eau.set_scale(facteurdecalibrationjaugeeau);
  float reservoir = (scale_eau.get_units()+236); 
  scale_eau.set_scale(facteurdecalibrationjaugeurine);
  float urine = scale_urine.get_units()-481;
  /// id_visaal 

  String Macaddresse = WiFi.macAddress();
  String Splitmac = Macaddresse.substring(10, 17);
  String Apssid = "Visaal_" + Splitmac;
  char Macsplit[16];
  Apssid.toCharArray(Macsplit, 16);
  char* soft_ap_ssid = Macsplit;
  const char* soft_ap_password = "NULL";
  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.softAP(soft_ap_ssid, NULL);
 

  /// id_visaal
  unsigned long duree = millis();
  if(etat_envoie){
    StaticJsonDocument<200> jsonDocument;
    jsonDocument["SSID"] = soft_ap_ssid; // Assign the Wi-Fi SSID to the "SSID" key
    jsonDocument["poche_eau"] = reservoir/1000;
    jsonDocument["poche_urine"] = urine/1000;
    String jsonString;
    serializeJson(jsonDocument, jsonString);
    client.publish("/visaal/poche-eau", jsonString);
    if (millis() - dernierTempsEnvoie >= 10000) {
    etat_envoie = false;
  }

  // Publish the JSON string to the MQTT topic
  }
  duree=millis();
  
  
  Serial.print(" niveau reservoire eau à instant t: ");
  Serial.println(reservoir);

  if((scale_eau.get_units()+236)<seuilreservoireau || (scale_urine.get_units()-481)>seuilreservoirurine){
    //client.publish("/hello", "alerte niveau eau ou niveau urine");
    digitalWrite(alerte, HIGH);
    delay(500);
    digitalWrite(alerte, LOW);
    delay(500);
  }

  unsigned long startTime = millis(); // Temps de début
  unsigned long duration = 15000;
  while(20<(scale_urine.get_units()-481) && (scale_urine.get_units()-481)<seuilreservoirurine && etataspiration){
    if(millis() - startTime > duration){
      digitalWrite(aspiration, HIGH);
      delay(5 * 1000);
      digitalWrite(aspiration, LOW);
      digitalWrite(rincage, HIGH);
      delay(5 * 1000);
      digitalWrite(rincage, LOW);
      delay(500);
      digitalWrite(aspiration, HIGH);
      delay(7 * 1000);
      digitalWrite(aspiration, LOW);
      etataspiration=false;
    }
  }
  startTime = millis();

  



}

