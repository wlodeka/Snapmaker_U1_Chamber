# Wersja do przeglądu właściciela

To uporządkowana kopia ostatniego potwierdzonego kodu Chamber z 17 października 2026: `esp32s3_chamber_U1_17102026_PREHEAT_WIFI_AP_OFF.ino`.

W repozytorium szkic ma prostszą nazwę: `firmware/Snapmaker_U1_Chamber/Snapmaker_U1_Chamber.ino`. Jego zawartość oraz nagłówek są identyczne z oryginałem. Nie zmieniono działającej logiki. Zachowano OTA, PREHEAT, TopCover i wyłączanie konfiguracyjnego punktu Wi-Fi po połączeniu z siecią.

Repozytorium jest prywatne. Pliki `.md` możesz później edytować bez zmiany firmware. Schematy dodaj do `docs/schematics`, zdjęcia do `docs/photos`, zrzuty interfejsu do `docs/screenshots`, mocowania do `docs/mechanical`, a listę elementów do `BOM`.

Plik aplikacji do OTA to `binaries/Snapmaker_U1_Chamber.ino.bin`. Login OTA: **chamber**, hasło ustawiasz w urządzeniu. Nie jest to kopia pamięci urządzenia: nie zawiera jego zapisanych ustawień NVS ani prywatnych haseł Wi-Fi/OTA.

Przed publicznym udostępnieniem pozostają: właściwy BOM i schemat, dokładny model płytki, wybór licencji oraz przegląd informacji o bezpieczeństwie. Dokumentacja uczciwie opisuje, że zwykłe sterowanie i zmiana ustawień nie mają logowania; urządzenie jest przeznaczone do zaufanej sieci lokalnej.
