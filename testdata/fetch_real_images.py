#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Descarga el corpus de fotografias REALES con el que se prueba la deteccion.

Por que un script y no las fotos en el repositorio:

- Son fotografias de terceros con licencia propia. Guardar el fichero obliga a
  arrastrar su atribucion en el arbol; guardar la RECETA deja la atribucion en un
  solo sitio, aqui, junto a la URL de la que salio.
- Pesan. Un repositorio de codigo que engorda con megas de JPEG se clona peor
  para siempre, porque git no olvida.

Las pruebas que usan este corpus SE SALTAN solas si no esta descargado, asi que
nadie se queda sin poder compilar por no tener red.

Uso:  python3 testdata/fetch_real_images.py
"""

import json
import os
import sys
import urllib.parse
import urllib.request

API = "https://commons.wikimedia.org/w/api.php"
UA = "pc-inspector-testdata/1.0 (proyecto de inspeccion visual; uso de prueba)"
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "real")

# Cada entrada dice POR QUE esta: un corpus sin ese porque acaba siendo una
# carpeta de fotos bonitas que no prueban nada.
CORPUS = [
    {
        "file": "Zirconium dioxide ZrO2 bearing balls.jpg",
        "as": "bolas_tres_sobre_negro.jpg",
        "why": "TRES piezas en un frame, sobre fondo negro, con reflejos especulares "
               "y una barra de escala de 10 mm. Prueba el recuento multiple y trae "
               "verdad de campo para calibrar. Ademas la barra y el texto NO son "
               "piezas: eso ninguna imagen sintetica lo prueba.",
    },
    {
        "file": "Silicon nitride Si3N4 bearing ball 10 mm G10.jpg",
        "as": "bola_oscura_sobre_claro_10mm.jpg",
        "why": "Pieza OSCURA sobre fondo CLARO — la polaridad contraria a todas las "
               "pruebas sinteticas del proyecto, que siempre fueron claro sobre "
               "oscuro. Con una regla real al lado y un diametro nominal conocido "
               "de 10 mm.",
    },
    {
        "file": "Silicon nitride Si3N4 bearing ball 20 mm G28.jpg",
        "as": "bola_oscura_sobre_claro_20mm.jpg",
        "why": "La misma escena con el doble de diametro: permite comprobar que lo "
               "medido ESCALA, que es mas fuerte que acertar un numero suelto.",
    },
    {
        "file": "5 Yen Heisei.png",
        "as": "moneda_5_yen_con_agujero.png",
        "why": "LA MEJOR VERDAD DE CAMPO DEL CORPUS: una moneda de 5 yenes mide 22,0 mm "
               "de diametro y su agujero central 5,0 mm, los dos publicados por la "
               "Casa de la Moneda de Japon. Y la razon entre ambos, 5/22 = 0,2273, es "
               "verdad de campo SIN ESCALA: se puede comprobar sin saber cuantos "
               "pixeles son un milimetro. Es el camino de la corona circular, que "
               "hasta ahora solo se probaba con dibujos generados aqui.",
    },
    {
        "file": "Insulating fiberglass washers for M3.jpg",
        "as": "arandelas_con_agujero.jpg",
        "why": "ESCENA HOSTIL, y esta aqui por eso. Se busco para probar el agujero "
               "pasante y resultó ser otra cosa: arandelas dentro de una bolsa de "
               "plastico, superpuestas, bajo una etiqueta impresa que ocupa media "
               "imagen y con reflejos encima. La respuesta correcta del programa aqui "
               "no es una medida buena: es no publicar una medida imposible ni "
               "presentar la etiqueta como si fuera la pieza.",
    },
    {
        "file": "Sprocket.jpg",
        "as": "pinon_corona_dentada.jpg",
        "why": "OTRA ESCENA HOSTIL: un monton de pinones amontonados en perspectiva, "
               "sin fondo, sin pieza aislada y con brillos metalicos por todas partes. "
               "No hay ninguna medida correcta que dar, y eso es justo lo que se "
               "comprueba: que no se de ninguna imposible.",
    },
    {
        "file": "Nut-hardware.jpg",
        "as": "tuerca_dominio_publico.jpg",
        "why": "Tuerca hexagonal con agujero pasante: contorno con hueco interior, "
               "para que el relleno del contorno exterior no pase por bueno.",
    },
]


def resolve(title, width=1600):
    params = {
        "action": "query",
        "format": "json",
        "titles": "File:" + title,
        "prop": "imageinfo",
        "iiprop": "url|size|extmetadata",
        "iiurlwidth": str(width),
    }
    url = API + "?" + urllib.parse.urlencode(params)
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=45) as response:
        data = json.load(response)
    pages = (data.get("query", {}) or {}).get("pages", {}) or {}
    for page in pages.values():
        info = (page.get("imageinfo") or [{}])[0]
        meta = info.get("extmetadata", {}) or {}
        return {
            "url": info.get("thumburl") or info.get("url"),
            "page": info.get("descriptionurl", ""),
            "license": (meta.get("LicenseShortName", {}) or {}).get("value", "?"),
            "author": (meta.get("Artist", {}) or {}).get("value", "?"),
        }
    return None


def main():
    os.makedirs(OUT, exist_ok=True)
    credits = []
    failed = []
    for item in CORPUS:
        target = os.path.join(OUT, item["as"])
        if os.path.exists(target) and os.path.getsize(target) > 20000:
            print("ya esta: %s" % item["as"])
            continue
        try:
            found = resolve(item["file"])
            if not found or not found["url"]:
                raise RuntimeError("sin URL")
            request = urllib.request.Request(found["url"], headers={"User-Agent": UA})
            with urllib.request.urlopen(request, timeout=90) as response:
                blob = response.read()
            # Una pagina de error tambien "se descarga": pesa dos kilos y no es
            # una imagen. Sin esta comprobacion, el corpus se llena de HTML.
            if len(blob) < 20000:
                raise RuntimeError("respuesta de %d bytes: no es la imagen" % len(blob))
            with open(target, "wb") as handle:
                handle.write(blob)
            print("descargado: %-38s %7d bytes  %s" % (item["as"], len(blob),
                                                       found["license"]))
            credits.append((item["as"], found["page"], found["license"]))
        except Exception as error:  # noqa: BLE001 - se reporta, no se esconde
            print("FALLO: %s (%s)" % (item["as"], error), file=sys.stderr)
            failed.append(item["as"])

    if credits:
        with open(os.path.join(OUT, "CREDITOS.txt"), "a", encoding="utf-8") as handle:
            for name, page, license_name in credits:
                handle.write("%s\n  origen: %s\n  licencia: %s\n\n"
                             % (name, page, license_name))
    print("\n%d descargadas, %d fallidas" % (len(credits), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
