# OBS Sport Eyes

**OBS Sport Eyes** è un filtro OBS per la ripresa automatica di eventi sportivi, progettato in particolare per il basket con camere panoramiche e 180°.

Il plugin usa modelli di object detection eseguiti tramite **OpenVINO** per rilevare i giocatori, individuare il nucleo dell’azione e generare un crop dinamico. L’obiettivo non è semplicemente seguire il soggetto più grande: è mantenere una ripresa leggibile, stabile nei possessi e pronta nei cambi di lato, limitando l’influenza di panchine, pubblico e rilevazioni marginali.

> Versione documentata: **v1.10.0n**
>
> Stato del progetto: sviluppo sperimentale e test sul campo. Prima di usarlo in una partita importante, prova sempre la stessa camera, risoluzione, posizione di montaggio e rete previste per la diretta.

---

## Cosa fa

- Rileva persone e oggetti con modelli OpenVINO.
- Genera un crop/zoom automatico per sorgenti molto larghe, incluse le riprese 180°.
- Supporta framing su singolo soggetto, soggetto più grande, tutti gli oggetti o **gruppo di giocatori**.
- Usa **group clustering** per dare priorità al nucleo dell’azione.
- Applica una **Safe ROI** per ridurre l’influenza di panchine, tribune e bordi del campo.
- Include **Director AI**, con predizione temporale di movimento e assistenza nelle transizioni rapide.
- Usa inferenza asincrona **latest-frame**, per evitare di inseguire frame ormai vecchi quando il sistema è sotto carico.
- Produce CSV diagnostici per analizzare latenza, tracking, crop e Director AI.
- Gestisce profili configurazione locali e file JSON importabili/esportabili.
- Mantiene la compatibilità con le scene create con il precedente source ID `detect-filter`.

---

## Architettura

```text
Sorgente video OBS
        │
        ▼
Pre-elaborazione / Safe ROI / scala inferenza
        │
        ▼
OpenVINO object detection
        │
        ├── SORT tracking
        ├── Group clustering
        └── Safe ROI filtering
        │
        ▼
Director AI + motion prediction
        │
        ▼
Crop controller / smoothing / deadband
        │
        ▼
Crop/Pad + Scale helper filters OBS
        │
        ▼
Output OBS / recording / streaming
```

### Inferenza asincrona latest-frame

Con **Async Inference** attiva, il thread video di OBS non attende il detector. Il worker mantiene al massimo:

- un’inferenza in corso;
- un solo frame pendente.

Quando il worker è occupato, un frame nuovo sostituisce quello pendente invece di creare una coda. Il sistema può quindi ridurre la frequenza effettiva di analisi sotto carico, ma evita di applicare crop basati su una sequenza sempre più vecchia di frame.

I risultati mantengono timestamp e origine ROI. **SORT** e **Director AI** ricevono soltanto detection fresche; i risultati cached servono alla continuità visiva e non vengono riusati come nuove osservazioni di velocità.

---

## Funzioni principali

### Tracking e framing sportivo

| Strategia | Uso pratico |
|---|---|
| **Single first** | Segue il primo oggetto valido. |
| **Biggest** | Privilegia la rilevazione più grande. |
| **Oldest** | Mantiene l’oggetto tracciato da più tempo. |
| **All** | Considera l’insieme delle rilevazioni. |
| **Group** | Crea cluster di giocatori e seleziona il gruppo più rilevante. |

Per il basket, **Group** è normalmente l’impostazione consigliata: evita di inseguire un singolo atleta e tende a mantenere dentro il crop l’area dove si sviluppa il possesso.

### Safe ROI

La Safe ROI definisce margini sinistro, destro, superiore e inferiore trattati con prudenza. È utile quando la camera include:

- panchine;
- arbitri e persone a bordo campo;
- pubblico;
- cartellonistica;
- ingressi laterali.

**Safe ROI Hold** mantiene per un breve periodo l’ultima decisione valida prima del fallback. **Cluster inertia** richiede una breve conferma prima di sostituire il gruppo attivo con un altro cluster concorrente.

### Director AI

Director AI aggiunge una previsione temporale al crop tradizionale. Stima:

- velocità e direzione dell’azione;
- probabilità di transizione rapida;
- centro previsto dell’inquadratura;
- anticipo massimo applicabile;
- confidence della misura.

Non sostituisce il detector: usa le detection come misura per ridurre il ritardo percepito nei contropiedi e nei cambi campo.

### Diagnostica CSV

Abilitando **CSV Logging** diventano disponibili due destinazioni:

- **CSV Diagnostica unificata**: oggetti, gruppo selezionato, Safe ROI, crop, stato tracking e telemetria async.
- **CSV Director AI**: centro misurato e predetto, velocità, confidence, lead, transizioni e piano crop.

I CSV servono per confrontare configurazioni e misurare oggettivamente latenza, jitter, overshoot, stabilità del gruppo e continuità delle detection.

---

## Profili di configurazione e JSON

Nelle proprietà del filtro è disponibile il gruppo **Configuration Profiles**.

### Profili inclusi

| Profilo | Quando usarlo |
|---|---|
| **Basket 180 – Balanced** | Punto di partenza consigliato: compromesso fra stabilità e reattività. |
| **Basket 180 – Reactive** | Per contropiedi e cambi campo frequenti; più pronto, ma potenzialmente più nervoso. |
| **Basket 180 – Conservative** | Per camere o palestre con detection instabili; privilegia una ripresa ferma. |

I preset modificano il tuning sportivo, preservando modello selezionato, device OpenVINO, percorso del modello esterno e percorsi CSV locali.

### Libreria locale, import ed export

- **Apply selected profile** applica un preset o un profilo locale salvato.
- **Save / update profile** salva la configurazione corrente nella libreria del plugin; lo stesso nome aggiorna il profilo esistente.
- **Delete selected saved profile** elimina solo profili locali creati dall’operatore.
- **Export current configuration** crea un JSON portabile.
- **Import and apply JSON** controlla e applica un JSON; poi puoi salvarlo nella libreria locale.

Il formato è versionato:

```json
{
  "format": "obs-sport-eyes-profile",
  "schema_version": 1,
  "profile_name": "Basket 180 - Palestra",
  "settings": {
    "tracking_group": true,
    "zoom_object": "group",
    "async_inference_enabled": true,
    "infer_interval_ms": 50,
    "director_ai_enabled": true
  }
}
```

L’import applica soltanto impostazioni supportate. Non importa scene OBS, filtri helper, oggetti rilevati, cache async o altro stato runtime.

> **Compatibilità v1.10.0n:** l’export costruisce i valori effettivi combinando default e configurazione corrente con API `obs_data` disponibili anche negli SDK OBS che non espongono `obs_data_get_json_with_defaults`.

### Profilo PanaCast 180 4K

Nel repository è disponibile anche:

```text
profiles/PanaCast_180_4K_5m_6m_Sideline.json
```

È un punto di partenza per una **Jabra PanaCast 180 4K** montata circa **5 m** sopra il campo e **6 m** oltre la linea laterale. Mantiene un crop leggermente più ampio, protegge i margini laterali e la fascia bassa esposta a panchine e persone fuori dal gioco, senza rinunciare alla reattività nelle transizioni laterali.

---

## Configurazione iniziale consigliata per basket 180°

```text
Async Inference:        ON
Infer interval:          50 ms
Infer scale:             1.00
Director AI:             ON
Zoom object:             Group
Zoom factor:             0.50
Safe ROI Hold:           300 ms
Cluster inertia:         150 ms
```

Questi valori sono un punto di partenza, non un preset universale. Il tuning dipende da risoluzione, FPS, posizione della camera, porzione di campo inquadrata, qualità del modello e potenza CPU/GPU.

---

## Prerequisiti Windows x64

### Software richiesto

- Git for Windows
- Visual Studio 2022 Community oppure Build Tools 2022 con **Desktop development with C++**
- Windows 10/11 SDK e toolset **MSVC v143 x64/x86**
- CMake nel PATH
- PowerShell 7
- OBS Studio x64
- OpenVINO Runtime

Nel Visual Studio Installer verifica almeno:

```text
Desktop development with C++
MSVC v143 - VS 2022 C++ x64/x86 build tools
Windows 10 SDK oppure Windows 11 SDK
C++ CMake tools for Windows
```

### OpenVINO

La configurazione usata nel progetto può puntare direttamente al runtime Intel installato, ad esempio:

```text
C:\Program Files\Intel\openvino_2024\runtime\cmake\OpenVINOConfig.cmake
```

In alternativa puoi usare un’installazione OpenVINO gestita da vcpkg.

Verifica il percorso Intel:

```powershell
Test-Path "C:\Program Files\Intel\openvino_2024\runtime\cmake\OpenVINOConfig.cmake"
```

L’output atteso è `True`.

---

## Recuperare il sorgente

```powershell
cd C:\Users\MITRO\Downloads\dev
git clone https://github.com/mitro72/OBS-SPORT-EYES.git
cd .\OBS-SPORT-EYES
```

Se la repository è privata, Git richiederà l’autenticazione GitHub.

---

## Compilazione su Windows

Dalla root della repository:

```powershell
cd C:\Users\MITRO\Downloads\dev\OBS-SPORT-EYES

$env:OpenVINO_DIR = "C:\Program Files\Intel\openvino_2024\runtime\cmake"

pwsh .\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

In alternativa, se usi vcpkg:

```powershell
$env:OpenVINO_DIR = "C:\vcpkg\installed\x64-windows\share\openvino"
pwsh .\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

L’output atteso include:

```text
release\RelWithDebInfo\obs-plugins\64bit\obs-sport-eyes.dll
```

### Problemi comuni

| Sintomo | Causa probabile | Correzione |
|---|---|---|
| `OpenVINOConfig.cmake` non trovato | `OpenVINO_DIR` errato o runtime assente | Verifica il percorso Intel OpenVINO o quello vcpkg. |
| `LibObsConfig.cmake` non trovato | Configurazione incompleta o dipendenze OBS non recuperate | Elimina `build_x64` e rilancia la build con connessione disponibile. |
| `cl.exe` / compilatore C++ non trovato | Workload C++ o SDK mancanti | Installa Desktop development with C++. |
| `pwsh` non riconosciuto | PowerShell 7 non installato o non nel PATH | Installa PowerShell 7 e riapri il terminale. |
| `obs_data_get_json_with_defaults` non trovato | Codice precedente alla correzione v1.10.0n | Aggiorna `SportEyesProfileManager.cpp` alla versione v1.10.0n. |

---

## Installazione in OBS

1. Chiudi OBS completamente.
2. Fai una copia di sicurezza della DLL precedente e della relativa cartella dati.
3. Copia `obs-sport-eyes.dll` in:

```text
C:\Program Files\obs-studio\obs-plugins\64bit\
```

4. Copia la cartella dati generata in:

```text
C:\Program Files\obs-studio\data\obs-plugins\obs-sport-eyes\
```

5. Riavvia OBS e aggiungi il filtro **Sport Eyes** alla sorgente video.

### Migrazione dal vecchio modulo

Il modulo registra l’identità `obs-sport-eyes` e anche il legacy source ID `detect-filter`, così le scene esistenti possono continuare a funzionare.

Non lasciare contemporaneamente nella cartella plugin:

```text
obs-detect.dll
obs-sport-eyes.dll
```

Entrambe possono cercare di registrare `detect-filter`. Mantieni una copia del vecchio plugin fuori dalla cartella OBS e installa solo la nuova DLL.

---

## Struttura del codice

```text
src/
├── config/                  # Profili locali, import ed export JSON
├── filter/                  # Proprietà OBS, default, lifecycle e start-up tracking
├── pipeline/                # video_tick e inferenza asincrona latest-frame
├── sport/                   # Group clustering e logica Safe ROI
├── director_ai/             # Stato temporale, planner camera e previsione
├── diagnostics/             # Scrittura CSV
├── render/                  # Render OBS del filtro
├── sort/                    # Multi-object tracking SORT
├── edgeyolo/                # Detector OpenVINO EdgeYOLO
├── yunet/                   # Adapter YuNet / OpenVINO
└── obs-utils/               # Helper OBS

profiles/                    # JSON pronti da importare
```

`detect-filter.cpp` resta una facciata OBS sottile; pipeline, GUI, rendering, clustering, lifecycle e gestione dei profili sono isolati in moduli distinti.

---

## Test e diagnostica

Per valutare modifiche o profili, usa una clip di test ripetibile e confronta:

- ritardo del crop nei cambi di lato;
- jitter durante possessi statici;
- numero e ampiezza delle micro-correzioni;
- overshoot dopo un contropiede;
- tempo di recupero dopo errori di detection;
- età del risultato d’inferenza;
- stabilità del gruppo selezionato;
- frequenza di `safe_roi_hold` e detection vuote.

Con Async Inference attiva, sotto carico il sistema può analizzare meno frame, ma non dovrebbe creare una coda di crop basati su frame progressivamente più vecchi.

---

## Limitazioni note

- Il risultato dipende fortemente da posizione, altezza, ottica e qualità della camera.
- Il rilevamento persone non equivale al riconoscimento di palla, canestro, squadre o statistiche di gioco.
- Una scala di inferenza più alta può aumentare il dettaglio, ma richiede più risorse e può ridurre la frequenza effettiva di analisi.
- Director AI può migliorare la reattività, ma parametri troppo aggressivi producono overshoot o instabilità.
- La pipeline asincrona privilegia la freschezza del frame rispetto alla copertura di ogni singolo frame: è una scelta deliberata per lo streaming live.
