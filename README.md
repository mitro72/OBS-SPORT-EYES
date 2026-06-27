# OBS Sport Eyes

**OBS Sport Eyes** è un filtro OBS pensato per la ripresa automatica di eventi sportivi, con particolare attenzione al basket e alle camere panoramiche / 180°.

Il plugin analizza il video tramite modelli di object detection eseguiti con **OpenVINO**, individua i giocatori e costruisce un'inquadratura dinamica da applicare alla sorgente. L'obiettivo non è semplicemente “seguire l'oggetto più grande”, ma mantenere una ripresa leggibile dell'azione: stabile nei possessi, pronta sui cambi di lato e resistente a panchine, pubblico e rilevazioni marginali.

Questa repository contiene la versione **1.10.0m** del progetto.

> Stato del progetto: sviluppo sperimentale, orientato a test sul campo. Prima di usare una build in una partita importante, eseguire sempre una prova con la stessa camera, risoluzione e rete previste per la diretta.

---

## Cosa fa

- Rileva persone e oggetti tramite modelli OpenVINO.
- Genera un crop/zoom automatico per sorgenti video molto larghe, incluse riprese 180°.
- Supporta modalità di selezione basate su singolo soggetto, soggetto più grande, tutti gli oggetti o **gruppo di giocatori**.
- Usa clustering di prossimità per dare priorità al nucleo dell'azione.
- Applica una **Safe ROI** per limitare l'influenza di panchine, tribune e zone laterali indesiderate.
- Integra **Director AI**, un livello di previsione temporale che stima movimento, direzione dell'azione e transizioni rapide.
- Include inferenza realmente asincrona con politica **latest-frame**, progettata per evitare che il tracking insegua frame vecchi quando il sistema è sotto carico.
- Espone log CSV per analizzare stabilità, crop, rilevamenti e comportamento del Director AI.
- Mantiene la compatibilità con le scene create con il precedente filtro `detect-filter`.

---

## Architettura della soluzione

```text
Sorgente video OBS
        │
        ▼
Pre-elaborazione / ROI / scala inferenza
        │
        ▼
OpenVINO object detection
        │
        ├── SORT tracking
        ├── Group clustering
        └── Safe ROI filtering
        │
        ▼
Director AI + Motion prediction
        │
        ▼
Crop controller / smoothing / deadband
        │
        ▼
Filtro crop e scale OBS
        │
        ▼
Output OBS / recording / streaming
```

### Inferenza asincrona “latest-frame”

Con **Async Inference** attiva, il thread video di OBS non rimane bloccato in attesa dell'inferenza.

Il worker mantiene al massimo:

- un'inferenza in corso;
- un solo frame pendente.

Quando il worker è occupato, un frame nuovo sostituisce il frame pendente precedente. In questo modo il sistema può ridurre la frequenza effettiva di analisi sotto carico, ma evita di accumulare una coda di frame datati che produrrebbe un crop sempre in ritardo rispetto all'azione reale.

I risultati portano con sé timestamp di cattura e origine della ROI. SORT e Director AI ricevono solo misure nuove; i risultati memorizzati vengono usati per continuità visiva, senza falsare il calcolo della velocità.

---

## Funzioni principali

### Tracking e framing sportivo

Il filtro può seguire diverse strategie di inquadratura:

- **Single first**: privilegia il primo oggetto valido.
- **Biggest**: segue la rilevazione più grande.
- **Oldest**: mantiene l'oggetto tracciato da più tempo.
- **All**: considera l'insieme delle rilevazioni.
- **Group**: crea cluster di giocatori e seleziona il gruppo più rilevante per l'azione.

La modalità **Group** è la più adatta al basket: invece di inseguire un singolo atleta, tende a inquadrare il gruppo che rappresenta il possesso o la zona attiva del campo.

### Safe ROI

La Safe ROI definisce margini sinistro, destro, superiore e inferiore da trattare con maggiore prudenza. È utile soprattutto quando la camera riprende anche:

- panchine;
- arbitri o persone a bordo campo;
- pubblico;
- cartellonistica;
- ingressi laterali.

Il parametro **Safe ROI Hold** evita che il crop reagisca immediatamente a un oggetto esterno alla zona utile, mentre **Cluster inertia** riduce i cambi continui tra gruppi simili.

### Director AI

Director AI aggiunge una previsione temporale al crop tradizionale. Usa le misure recenti per stimare:

- velocità orizzontale dell'azione;
- direzione del movimento;
- transizioni rapide;
- centro previsto dell'inquadratura;
- anticipo massimo applicabile.

Non sostituisce il detector: usa il detector come misura e interviene per ridurre il ritardo percepito nelle azioni veloci.

### Diagnostica CSV

Il checkbox **Abilita log CSV** rende visibili due percorsi configurabili:

- **CSV Diagnostica unificata**: rilevazioni, oggetti, gruppo selezionato, Safe ROI, crop e stato del tracking.
- **CSV Director AI**: centro misurato/predetto, velocità, confidence, lead, transizioni e piano di crop.

I CSV sono pensati per confrontare configurazioni diverse e misurare oggettivamente ritardo, jitter, overshoot e stabilità del framing.

---

## Interfaccia e parametri chiave

### Tracking

| Parametro | Funzione pratica |
|---|---|
| **Zoom factor** | Quanta parte della sorgente viene mantenuta nell'inquadratura. |
| **Zoom speed factor** | Velocità di adattamento dello zoom. |
| **X pan preset** | Modalità di posizionamento orizzontale: Auto, Left, Center, Right, Auto Snap e Auto Snap Smooth. |
| **X snap hysteresis** | Evita passaggi troppo frequenti tra aree laterali e centro. |
| **Async Inference** | Attiva il worker di inferenza non bloccante. |
| **Infer interval** | Intervallo minimo tra richieste di inferenza. |
| **Infer scale** | Scala del frame usata dal detector; valori più alti aumentano dettaglio e carico. |
| **X deadband** | Zona morta che riduce micro-correzioni del pan. |
| **Zoom object** | Strategia di selezione: singolo, più grande, più vecchio, tutti o gruppo. |

### Group / Safe ROI

| Parametro | Funzione pratica |
|---|---|
| **Group min people** | Numero minimo di persone necessario per validare un gruppo. |
| **GroupMaxDistFrac** | Distanza massima fra persone dello stesso cluster, espressa come frazione della larghezza frame. |
| **Strict min people** | Richiede rigidamente il numero minimo di persone. |
| **Safe ROI margins** | Margini da proteggere da rilevazioni laterali o non pertinenti. |
| **Safe ROI Hold** | Tempo di conferma prima di reagire a eventi nella zona protetta. |
| **Cluster inertia** | Riduce cambi rapidi tra cluster concorrenti. |

### Director AI

| Parametro | Funzione pratica |
|---|---|
| **Prediction horizon** | Anticipo desiderato, in millisecondi, rispetto alla misura rilevata. |
| **Velocity smoothing** | Filtra la velocità stimata; valori più alti reagiscono più rapidamente ma possono aumentare instabilità. |
| **History samples** | Numero di campioni usati per la stima temporale. |
| **Base coverage** | Copertura minima dell'azione desiderata nel crop. |
| **Fast transition speed** | Soglia oltre cui il movimento viene trattato come transizione veloce. |
| **Max prediction lead** | Limite massimo dell'anticipo spaziale applicabile. |
| **Min confidence** | Confidence minima per applicare la previsione. |

---

## Profili di configurazione e JSON

Nelle proprietà del filtro è disponibile il gruppo **Configuration Profiles**.

- I preset **Basket 180 - Balanced**, **Reactive** e **Conservative** applicano tuning sportivo senza modificare modello, device, percorso del modello esterno o destinazioni CSV locali.
- **Save / update profile** salva la configurazione corrente nella libreria locale del plugin; un nome già esistente viene aggiornato.
- **Apply selected profile** applica un preset o un profilo salvato.
- **Export current configuration** crea un JSON portabile.
- **Import and apply JSON** convalida e applica un JSON; poi **Save / update profile** lo aggiunge alla libreria locale.

I JSON sono versionati tramite `format: "obs-sport-eyes-profile"` e `schema_version: 1`. Vengono importati solo i parametri supportati; non vengono importati scene OBS, filtri helper o stato runtime. La documentazione del formato è in [`docs/README_v1.10.0m_Configuration_Profiles.md`](docs/README_v1.10.0m_Configuration_Profiles.md).

## Configurazione iniziale consigliata

Per una prima prova con basket e sorgente 180°:

```text
Async Inference:        ON
Async Motion Prediction: ON
Infer interval:          50 ms
Infer scale:             1.00
Director AI:             ON
Zoom object:             Group
Safe ROI Hold:           300 ms
Cluster inertia:         150 ms
```

Questi valori sono un punto di partenza, non un preset universale. La configurazione dipende da risoluzione, FPS, posizione della camera, larghezza del campo visibile, potenza GPU/CPU e modello di rilevazione.

---

## Installazione e prerequisiti (Windows x64)

Questa sezione descrive l'ambiente consigliato per compilare e usare OBS Sport Eyes su Windows 10/11 a 64 bit. È pensata per una macchina di sviluppo locale, non per distribuire dipendenze dentro OBS.

### 1. Software richiesto

Installa questi componenti prima di configurare il progetto:

- **Git for Windows**, necessario per clonare/aggiornare repository e per gli script che recuperano dipendenze.
- **Visual Studio 2022 Community** o Build Tools 2022, con il workload **Desktop development with C++**.
- **Windows 10/11 SDK** e toolset **MSVC v143 x64/x86**.
- **CMake** recente, disponibile nel PATH.
- **PowerShell 7**, usato dallo script `Build-Windows.ps1`.
- **vcpkg**, usato per installare OpenVINO e rendere disponibile il relativo package CMake.
- **OBS Studio x64** per eseguire il plugin compilato.

Nel Visual Studio Installer verifica in particolare questi componenti:

```text
Desktop development with C++
MSVC v143 - VS 2022 C++ x64/x86 build tools
Windows 10 SDK oppure Windows 11 SDK
C++ CMake tools for Windows
```

Verifica da PowerShell che gli strumenti siano visibili:

```powershell
Git --version
cmake --version
pwsh --version
```

> È normale che `cl.exe` non sia disponibile in una PowerShell normale: CMake richiama automaticamente Visual Studio 2022 tramite il proprio generatore/preset.

### 2. Installare o inizializzare vcpkg

La configurazione di riferimento usa vcpkg in:

```text
C:\vcpkg
```

Se non hai già vcpkg:

```powershell
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd C:\vcpkg
.\bootstrap-vcpkg.bat
```

Verifica l'installazione:

```powershell
C:\vcpkg\vcpkg.exe version
```

### 3. Installare OpenVINO tramite vcpkg

Installa OpenVINO per la tripla Windows x64:

```powershell
cd C:\vcpkg
.\vcpkg.exe install openvino:x64-windows
```

Al termine, il file cercato da CMake deve essere presente qui:

```text
C:\vcpkg\installed\x64-windows\share\openvino\OpenVINOConfig.cmake
```

Verifica esplicitamente:

```powershell
Test-Path "C:\vcpkg\installed\x64-windows\share\openvino\OpenVINOConfig.cmake"
```

L'output atteso è:

```text
True
```

Prima di compilare il plugin, nella stessa finestra PowerShell imposta:

```powershell
$env:OpenVINO_DIR = "C:\vcpkg\installed\x64-windows\share\openvino"
```

Puoi controllare il valore effettivo:

```powershell
$env:OpenVINO_DIR
```

#### Aggiornare OpenVINO con vcpkg

Prima aggiorna il repository dei port e controlla il piano di aggiornamento:

```powershell
cd C:\vcpkg
git pull
.\vcpkg.exe update
.\vcpkg.exe upgrade openvino:x64-windows
```

L'ultimo comando è un **dry-run**: mostra cosa cambierà senza installare nulla. Per confermare l'upgrade:

```powershell
.\vcpkg.exe upgrade openvino:x64-windows --no-dry-run
```

Dopo l'aggiornamento, elimina `build_x64` del plugin e riconfigura CMake, per evitare riferimenti a librerie OpenVINO precedenti.

### 4. Recuperare il sorgente

Clona la repository o usa una copia locale già estratta:

```powershell
cd C:\Users\MITRO\Downloads\dev
git clone https://github.com/mitro72/OBS-SPORT-EYES.git
cd .\OBS-SPORT-EYES
```

Se la repository è privata, Git richiederà l'autenticazione GitHub. Controlla di essere nella root del progetto: devono essere presenti `CMakeLists.txt`, `CMakePresets.json`, `buildspec.json` e `src\`.

### 5. Dipendenze risolte dalla build

Non è necessario installare manualmente `libobs` o OpenCV come pacchetti di sistema. Lo script/preset di build risolve le dipendenze previste dal progetto, incluse quelle OBS e OpenCV, durante la prima configurazione; per questo la prima build richiede connessione Internet.

OpenVINO è l'eccezione principale: deve essere già disponibile tramite vcpkg e raggiungibile con `OpenVINO_DIR`.

### 6. Verifica prima della compilazione

Dalla root del progetto esegui:

```powershell
$env:OpenVINO_DIR = "C:\vcpkg\installed\x64-windows\share\openvino"
Test-Path "$env:OpenVINO_DIR\OpenVINOConfig.cmake"
```

Se il secondo comando restituisce `False`, non avviare la build: correggi prima l'installazione OpenVINO/vcpkg.

### Problemi comuni di prerequisiti

| Sintomo | Causa probabile | Correzione |
|---|---|---|
| `OpenVINOConfig.cmake` non trovato | `OpenVINO_DIR` non impostato o package non installato | Installa `openvino:x64-windows`, poi imposta `OpenVINO_DIR`. |
| `LibObsConfig.cmake` non trovato | Configurazione incompleta o dipendenze OBS non recuperate | Elimina `build_x64` e rilancia lo script con Internet disponibile. |
| `cl.exe` / compilatore C++ non trovato | Workload C++ o SDK mancanti | Modifica Visual Studio 2022 e installa Desktop development with C++. |
| `pwsh` non riconosciuto | PowerShell 7 non installato o non nel PATH | Installa PowerShell 7 e riapri il terminale. |
| DLL caricata ma OpenVINO non parte | Runtime OpenVINO non raggiungibile dall'ambiente | Verifica installazione x64 e la presenza delle DLL OpenVINO nel PATH/runtime previsto. |

---

## Compilazione su Windows

Dalla root della repository:

```powershell
cd C:\percorso\OBS-SPORT-EYES
$env:OpenVINO_DIR = "C:\vcpkg\installed\x64-windows\share\openvino"
pwsh .\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

In alternativa, usando CMake direttamente:

```powershell
cmake --preset windows-x64 -DOpenVINO_DIR="$env:OpenVINO_DIR"
cmake --build --preset windows-x64 --config RelWithDebInfo --parallel
cmake --install build_x64 --prefix .\release\RelWithDebInfo --config RelWithDebInfo
```

L'output atteso include:

```text
release\RelWithDebInfo\obs-plugins\64bit\obs-sport-eyes.dll
```

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

Il modulo installa la nuova identità `obs-sport-eyes` e registra anche il vecchio source ID `detect-filter` per preservare le scene esistenti.

Non lasciare contemporaneamente nella cartella plugin:

```text
obs-detect.dll
obs-sport-eyes.dll
```

Entrambe potrebbero cercare di registrare il legacy ID `detect-filter`. Durante la migrazione, conserva una copia di backup del vecchio plugin fuori dalla cartella di OBS e installa solo la nuova DLL.

---

## Struttura del codice

```text
src/
├── filter/                 # Proprietà OBS, default, lifecycle e start-up tracking
├── pipeline/               # video_tick e inferenza asincrona latest-frame
├── sport/                  # Group clustering e logica Safe ROI collegata
├── director_ai/            # Stato temporale, planner camera, previsione e log Director
├── diagnostics/            # Scrittura log CSV
├── render/                 # Render OBS del filtro
├── sort/                   # Multi-object tracking SORT
├── edgeyolo/               # Detector OpenVINO EdgeYOLO
├── yunet/                  # Adapter YuNet / OpenVINO
└── obs-utils/              # Helper OBS
```

L'obiettivo della separazione è mantenere `detect-filter.cpp` come sottile facciata OBS, lasciando pipeline, GUI, rendering, clustering e lifecycle in moduli distinti e più verificabili.

---

## Test e diagnostica

Per valutare una modifica non basarti solo sulla percezione live. Usa una clip di test ripetibile e confronta:

- ritardo del crop rispetto al cambio di lato;
- numero e ampiezza delle micro-correzioni;
- jitter nel possesso statico;
- overshoot dopo un contropiede;
- tempo di recupero dopo rilevazioni errate o perdita del gruppo;
- età del risultato di inferenza;
- stabilità del gruppo selezionato.

Con Async Inference attiva, sotto carico il sistema può eseguire meno inferenze, ma non dovrebbe degradare in una sequenza di crop basati su frame sempre più vecchi.

---

## Limitazioni note

- Il risultato dipende fortemente dalla qualità e dall'angolo della camera.
- Il rilevamento persone non equivale a riconoscimento del pallone, del canestro, delle squadre o delle azioni statistiche.
- Una risoluzione di inferenza più alta migliora il dettaglio, ma richiede più risorse e può ridurre la frequenza effettiva di analisi.
- Director AI può migliorare la reattività, ma parametri troppo aggressivi producono overshoot o instabilità.
- La pipeline asincrona privilegia la freschezza del frame rispetto alla copertura di ogni singolo frame: è una scelta deliberata per lo streaming live.

---

## Origine e licenza

OBS Sport Eyes deriva dal lavoro svolto sul progetto `obs-detect` e include modifiche specifiche per la ripresa sportiva, la gestione di camere panoramiche, il clustering e il Director AI.

Il repository è distribuito secondo i termini della **GNU General Public License v2.0**. Consultare il file `LICENSE` per il testo completo.

---

## Contributi

Per rendere una modifica verificabile:

1. descrivere il problema osservato e le condizioni del test;
2. indicare camera, risoluzione, FPS, backend di inferenza e parametri rilevanti;
3. allegare, quando possibile, clip breve e CSV diagnostici;
4. evitare modifiche che mescolino refactor, tuning e nuove funzionalità nello stesso commit;
5. confrontare sempre il comportamento con una configurazione di riferimento.

