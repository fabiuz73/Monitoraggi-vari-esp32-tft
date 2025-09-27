# Monitoraggi-vari
display per monitoraggi vari 
# Icone LVGL per Allarme Gas Camper

Questo repository contiene immagini in formato C per la libreria grafica [LVGL](https://lvgl.io/), pensate per sistemi embedded (come ESP32, STM32, ecc.) che visualizzano icone su display grafici.

## Come utilizzare queste icone

1. **Aggiungi i file al tuo progetto LVGL**  
   Copia i file nel tuo progetto e includili dove desideri utilizzare le icone.

2. **Includi gli header necessari**
   ```c
   #include "icon_weather.c"
   #include "monitoraggio_gas.c"
   ```

3. **Crea un widget immagine in LVGL**
   Esempio:
   ```c
   lv_obj_t * img = lv_img_create(lv_scr_act());
   lv_img_set_src(img, &icon_weather);
   lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
   ```

## Opzioni disponibili

Il progetto offre funzioni per:

- **Visualizzazione meteo**  
  Utilizza l’icona meteo e può essere esteso per mostrare informazioni come temperatura, umidità e condizioni atmosferiche tramite l’integrazione con sensori o API meteo.

- **Monitoraggio del gas**  
  Visualizza lo stato dei sensori di gas in tempo reale, avvisa in caso di rilevazione di gas pericolosi e mostra lo stato tramite icone dedicate.

## Sviluppi futuri

Lo scopo del progetto è di essere facilmente estendibile:  
In futuro sarà possibile aggiungere nuove funzioni, come ad esempio:

- Visualizzazione di altre tipologie di allarmi (fumo, CO, batteria, ecc.)
- Integrazione con altri sensori o dispositivi smart per camper
- Personalizzazione delle icone e delle schermate
- Altre funzioni legate alla sicurezza e al comfort del camper

## Requisiti

- [LVGL v8.x](https://github.com/lvgl/lvgl)
- Un microcontrollore o sistema embedded compatibile con LVGL

## Note tecniche

- Le icone sono incorporate come array di byte (C source), ottimizzate per essere incluse direttamente nel firmware (nessun filesystem richiesto).
- Il formato delle icone è ARGB8888, 64x64 pixel, per un totale di `4096 * 4` bytes.

## Autori

- fabiuz73

## Licenza

Questo repository è distribuito con licenza MIT. Consulta il file LICENSE per maggiori dettagli.

## Contatti

Per domande o suggerimenti, apri una issue su GitHub.
