
<img width="6000" height="4000" alt="_DSC2154" src="https://github.com/user-attachments/assets/b6c2f082-f53b-4027-9c4c-1df20af39448" />

<img width="6000" height="4000" alt="_DSC2155" src="https://github.com/user-attachments/assets/bbb14f80-18c5-4be0-aef2-0803967c756e" />

## Vidéo sur Youtube :

<img width="751" height="430" alt="youtube" src="https://github.com/user-attachments/assets/3d057381-b199-4ccc-ac73-819acd0305f9" />


[![RailCom Detector Demo](https://img.youtube.com/vi/wCtRoU4dOLk/maxresdefault.jpg)](https://youtu.be/wCtRoU4dOLk)

Cliquez sur l'image pour lancer la vidéo.

# Railcom-Detector-Display
RailCom Detector / Display est un projet open source basé sur un ESP32 permettant de recevoir, décoder et afficher en temps réel les informations transmises par les décodeurs DCC compatibles RailCom®.

## Fonctionnalités

* Réception des messages RailCom à 250 kbauds.
* Décodage du codage NMRA RailCom « 4-out-of-8 ».
* Détection et affichage des adresses courtes et longues DCC.
* Filtrage logiciel des parasites et des lectures erronées par buffer circulaire.
* Affichage sur un module 7 segments 4 digits multiplexé.

## Architecture logicielle

Le programme est organisé autour de plusieurs tâches FreeRTOS :

* **ReceiveData** : réception des données RailCom.
* **ParseData** : décodage des trames RailCom et extraction des adresses DCC.
* **DisplayAddress** : gestion de l'affichage multiplexé.

## Matériel utilisé

* ESP32-WROOM-32
* Détecteur RailCom compatible NMRA : https://www.locoduino.org/spip.php?article334
* Afficheur LED 7 segments 4 digits : LFD039AUE-102A https://www.tme.eu/en/details/lfd039aue-102a/7-segment-led-displays/wenrun/lfd039aue-102a-01/

Voici le schéma :

<img width="1040" height="653" alt="railcom_display_v2_1" src="https://github.com/user-attachments/assets/05c662f0-8adb-4191-9a12-c7eeaa9e8d02" />

Les fichiers GERBER sont disponibles dans la liste ci-dessus : afficheurRailcom7segments_Gerber.zip

<img width="740" height="524" alt="railcom_display" src="https://github.com/user-attachments/assets/eedcfc62-5e66-4913-be25-d795183188fd" />

