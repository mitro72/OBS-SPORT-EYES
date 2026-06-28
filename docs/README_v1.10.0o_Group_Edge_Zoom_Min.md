# OBS Sport Eyes v1.10.0o – Group Edge Zoom Min

## Obiettivo

Aggiunge il parametro **Group Edge Zoom Min (x)** alla modalità Group Edge Zoom 2D.

Prima della modifica, il moltiplicatore di zoom era sempre `1.00` vicino al centro del campo e cresceva fino a **Group Edge Zoom Max** quando il gruppo arrivava ai bordi.

Ora, quando **Group edge zoom 2D (U curve)** è abilitato e il cluster del gruppo è valido:

- **Group Edge Zoom Min (x)** è il moltiplicatore minimo vicino al centro;
- **Group Edge Zoom Max (x)** resta il moltiplicatore massimo ai lati;
- **Group Edge Zoom Curve** definisce quanto gradualmente la transizione passa dal minimo al massimo;
- **Group Edge Zoom Smooth (s)** continua a smussare i cambi di dimensione crop.

## Retrocompatibilità

Il default è `1.00`. Con questo valore, l’equazione è identica al comportamento precedente:

```text
zoom = 1.00 + (max - 1.00) × edgeCurve
```

Per esempio, con `Min = 1.05` e `Max = 1.25`, una volta rilevato un vero cluster l’inquadratura mantiene almeno 1,05× di zoom 2D e cresce gradualmente fino a 1,25× verso i bordi.

Il parametro viene limitato automaticamente tra `1.00` e il valore massimo, così un JSON non può invertire la curva.

## Quando usarlo

- `1.00`: impostazione più sicura, identica alle build precedenti.
- `1.03–1.08`: utile con camera alta/laterale per evitare un ritorno troppo largo quando il gruppo rientra dal bordo.
- Oltre `1.10`: da usare solo dopo test video; può far apparire il crop troppo stretto in possesso statico.
<<<<<<< HEAD
=======

>>>>>>> fccbc72 (v1.10.0o profiles JSON and group edge zoom minimum)
