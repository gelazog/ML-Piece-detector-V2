// EL INFORME DE UNA PIEZA LISTABA LOS AGUJEROS DE LAS DEMÁS.
//
// Salió midiendo el banco de fotos pieza por pieza, que es exactamente para lo
// que está el banco. En `producto-tuercas-prueba.jpg` —una bandeja con muchas
// tuercas— el informe de la pieza medida decía, en la misma tabla:
//
//     [contorno] Agujeros            2
//     [cota]     Ø agujero 6      42,70 px
//     [cota]     Ø agujero 15     42,69 px
//     ... y así hasta CIENTO SESENTA Y NUEVE filas, numeradas hasta la 169.
//
// Los números no eran falsos: son agujeros de verdad, de tuercas de verdad, y
// como todas las tuercas se parecen salían casi iguales. Eran de OTRAS piezas.
// `findHoles` mira toda la máscara y la máscara lleva la bandeja entera,
// mientras que el resto del proponedor mira el contorno mayor —«esta pieza»—.
//
// En `arandelas-4.png` el mismo fallo tenía peor pinta todavía: cuatro filas
// llamadas las cuatro «Ø interior», que son cuatro arandelas distintas. Una
// corona tiene UN agujero central; cuatro cotas con el mismo nombre y valores
// distintos no las distingue nadie.
//
// Por qué esta prueba y no otra: el informe ya contaba los agujeros bien —los
// hechos del contorno decían «2»—, así que la incoherencia estaba DENTRO de una
// sola tabla y se podía comprobar sin verdad de campo. La regla es esa: un
// informe no puede publicar más diámetros de agujero que agujeros ha contado.

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "inspection_editor/auto_measure.h"
#include "inspection_editor/piece_report.h"
#include "vision/geometry_features.h"
#include "vision/segmentation.h"

using namespace pci;

namespace {

const std::filesystem::path kBank{"C:/Users/furro/Pictures/IMG-MC"};

bool isAHoleDimension(const std::string& name) {
    return name.rfind("Ø agujero", 0) == 0 || name == "Ø interior";
}

struct Piece {
    cv::Mat gray;
    cv::Mat mask;
    vision::ContourReport contour;
};

// Cada foto del banco, segmentada como la segmenta la aplicación.
std::vector<std::pair<std::string, Piece>> bank() {
    std::vector<std::pair<std::string, Piece>> all;
    std::error_code ec;
    if (!std::filesystem::exists(kBank, ec)) {
        return all;
    }
    for (const auto& entry : std::filesystem::directory_iterator(kBank, ec)) {
        Piece piece;
        piece.gray = cv::imread(entry.path().string(), cv::IMREAD_GRAYSCALE);
        if (piece.gray.empty()) {
            continue;
        }
        vision::SegmentationOptions options;
        options.recoverHighlightsBy = 12;
        const auto segmented = vision::segmentPiece(piece.gray, options);
        if (!segmented.isOk()) {
            continue;
        }
        piece.mask = segmented.value();
        piece.contour = vision::describeContour(piece.mask);
        if (!piece.contour.valid) {
            continue;
        }
        all.emplace_back(entry.path().filename().string(), std::move(piece));
    }
    return all;
}

}  // namespace

TEST(HoleDimensions, NoPieceIsOfferedMoreHolesThanItHas) {
    const auto photos = bank();
    if (photos.empty()) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }
    int worst = 0;
    for (const auto& [name, piece] : photos) {
        // Por el INFORME, que es lo que se lee en pantalla: `proposeTools` corta
        // en doce porque prepara una plantilla revisable, y con el corte puesto
        // las ciento sesenta y nueve filas no se ven. El informe de pieza no
        // corta —«se quiere entero, se lee de una vez y se exporta»— y por eso
        // es donde el fallo salía a la cara.
        const auto proposals =
            inspection::measureWholePiece(piece.gray, piece.mask, {}, 0.0,
                                          inspection::LengthUnit::Auto, piece.gray.size())
                .watchable;
        const int offered = static_cast<int>(std::count_if(
            proposals.begin(), proposals.end(),
            [](const auto& p) { return isAHoleDimension(p.config.name); }));
        const int counted = static_cast<int>(piece.contour.holes.size());
        std::printf("  [agujeros] %-30s contados %3d   ofrecidos %3d\n", name.c_str(),
                    counted, offered);
        worst = std::max(worst, offered - counted);
        EXPECT_LE(offered, counted)
            << name << ": el informe cuenta " << counted
            << " agujeros en la pieza y publica " << offered
            << " diámetros de agujero. Los que sobran son de otras piezas del "
               "encuadre, y la misma tabla se contradice dos filas más arriba";
    }
    std::printf("  [agujeros] el peor sobrante es %d\n", worst);
}

// UN AGUJERO NO PUEDE MEDIR MÁS QUE LA PIEZA QUE LO TIENE.
//
// No es un criterio de calidad, es geometría: el agujero está dentro, así que su
// diámetro no llega al lado corto del rectángulo que contiene la pieza.
//
// También salió del banco, y en la foto donde peor se veía: en `arandelas-4.png`
// la tabla ponía «Ø exterior 191,52 px» y justo debajo «Ø interior 191,47 px».
// El círculo se propone sobre el agujero que la máscara ve (Ø 150 px) y, al
// medirlo, la herramienta se va al borde de fuera. La avería de fondo —cada
// herramienta vuelve a umbralizar su propio contorno— está aparcada por decisión
// del dueño, pero publicar el número no es lo mismo que arreglarla.
//
// La regla ya existía escrita para otras cotas (`NoProposedToolPublishesAnImpossibleNumber`):
// no medir es una respuesta honesta; medir mal y decirlo con tres decimales, no.
TEST(HoleDimensions, NoHoleMeasuresAsMuchAsThePieceItIsIn) {
    const auto photos = bank();
    if (photos.empty()) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }
    int checked = 0;
    for (const auto& [name, piece] : photos) {
        const double fits = std::min(piece.contour.minRect.size.width,
                                     piece.contour.minRect.size.height);
        const auto proposals =
            inspection::measureWholePiece(piece.gray, piece.mask, {}, 0.0,
                                          inspection::LengthUnit::Auto, piece.gray.size())
                .watchable;
        for (const auto& p : proposals) {
            if (!isAHoleDimension(p.config.name) ||
                p.kind != inspection::MeasuredKind::Length) {
                continue;
            }
            ++checked;
            EXPECT_LT(p.measured, fits)
                << name << " / " << p.config.name << ": mide " << p.measured
                << " px dentro de una pieza cuyo lado corto son " << fits
                << " px. No está midiendo el agujero";
        }
    }
    std::printf("  [agujeros] %d cotas de agujero comprobadas contra su pieza\n", checked);
    EXPECT_GT(checked, 0) << "ninguna cota de agujero llegó a comprobarse: esta prueba "
                             "no está mirando donde cree";
}

// DOS FILAS «ÁREA» EN LA MISMA TABLA, CON NÚMEROS QUE NO SE PARECEN.
//
// El informe de pieza enseña cada magnitud dos veces a propósito: el área como
// HECHO del contorno y el área como COTA vigilada, que son cosas distintas —una
// describe, la otra juzga—. Lo que no puede pasar es que no se parezcan: si la
// tabla dice «Área 4294 px²» arriba y «Área 3126 px²» abajo, el operador no
// tiene forma de saber cuál apunta en el parte.
//
// Y no se parecían casi nunca. Medido foto a foto sobre el banco, antes de
// arreglarlo: en TRECE de diecisiete fotos la diferencia pasaba del 10 %, con
// 82 % en el área de `arandelas-2.png` —el contorno 5523 px², la cota 991— y
// 130 % en el perímetro de `tornillo-ojo-3.png`. La Región vuelve a umbralizar
// dentro de su recuadro y ahí ve otro borde, o el trozo de otra pieza que le
// cae dentro; con varias piezas en el encuadre eso es lo normal.
//
// Por qué esta prueba y no un aviso: un aviso saltaría en trece de diecisiete
// fotos, y un aviso que sale siempre se aprende a ignorar en dos días. La cota
// que no se parece a la pieza no se publica, y entonces la pieza se queda sin la
// cota que la vigila entera —que se nota, que es justo lo que se quiere—.
//
// El «Largo total» y el «Ancho total» sirven de testigo: esos salen de la
// geometría del contorno y no de volver a umbralizar, y coinciden al 0,0 % en
// las diecisiete. Si algún día esta prueba fallara por ellos, el problema sería
// otro muy distinto.
TEST(PieceReport, ACotaNamedLikeAContourFactHasToAgreeWithIt) {
    const auto photos = bank();
    if (photos.empty()) {
        GTEST_SKIP() << "las fotos del usuario no están en esta máquina";
    }
    int compared = 0;
    double worst = 0.0;
    for (const auto& [name, piece] : photos) {
        const auto report =
            inspection::measureWholePiece(piece.gray, piece.mask, {}, 0.0,
                                          inspection::LengthUnit::Auto, piece.gray.size());
        if (!report.ok) {
            continue;
        }
        for (const char* magnitude : {"Área", "Perímetro", "Largo total", "Ancho total"}) {
            double fact = -1.0;
            double cota = -1.0;
            for (const auto& row : report.rows) {
                if (row.tool != magnitude) {
                    continue;
                }
                if (row.group == inspection::kGroupContour) {
                    fact = row.value;
                } else if (cota < 0.0) {
                    cota = row.value;
                }
            }
            if (fact <= 0.0 || cota < 0.0) {
                continue;  // esa magnitud no sale por los dos caminos en esta pieza
            }
            ++compared;
            const double gap = std::abs(fact - cota) / fact;
            worst = std::max(worst, gap);
            EXPECT_LT(gap, 0.10)
                << name << ": la tabla enseña dos filas «" << magnitude
                << "», una de " << fact << " y otra de " << cota << " (" << 100.0 * gap
                << " % de diferencia). Con el mismo nombre y números distintos, no hay "
                   "manera de saber cuál va al parte";
        }
    }
    std::printf("  [informe] %d parejas hecho/cota comparadas, la peor difiere %.1f%%\n",
                compared, 100.0 * worst);
    EXPECT_GT(compared, 10) << "casi ninguna magnitud sale por los dos caminos: esta "
                               "prueba no está mirando donde cree";
}
