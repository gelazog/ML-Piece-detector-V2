#include "vision/lens_calibration.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace pci::vision {

namespace {

// Las esquinas del tablero en coordenadas del propio tablero, en milímetros y
// con Z = 0: es un plano, y el modelo de la cámara ya se encarga de dónde está
// ese plano en cada toma.
std::vector<cv::Point3f> boardPoints(const BoardSpec& spec) {
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<std::size_t>(spec.cornerCount()));
    for (int row = 0; row < spec.innerRows; ++row) {
        for (int col = 0; col < spec.innerCols; ++col) {
            points.emplace_back(static_cast<float>(col * spec.squareMm),
                                static_cast<float>(row * spec.squareMm), 0.0F);
        }
    }
    return points;
}

}  // namespace

bool BoardSpec::isValid() const {
    // Tres esquinas interiores por lado como mínimo: con dos, la rejilla no
    // tiene orientación única y `findChessboardCorners` puede devolverla girada
    // de una toma a otra.
    return innerCols >= 3 && innerRows >= 3 && squareMm > 0.0;
}

bool LensCalibration::isValid() const {
    return !cameraMatrix.empty() && cameraMatrix.rows == 3 && cameraMatrix.cols == 3 &&
           !distortion.empty() && !imageSize.empty() && views >= kMinimumViews &&
           reprojectionError > 0.0 && reprojectionError <= kMaxUsableReprojectionError;
}

std::optional<BoardView> findBoard(const cv::Mat& image, const BoardSpec& spec) {
    if (image.empty() || !spec.isValid()) {
        return std::nullopt;
    }
    cv::Mat gray;
    if (image.channels() == 1) {
        gray = image;
    } else if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        return std::nullopt;
    }

    BoardView view;
    view.imageSize = gray.size();
    // ACCURACY + NORMALIZE_IMAGE: lo primero rechaza rejillas mal formadas en
    // vez de devolver esquinas plausibles y equivocadas, y lo segundo hace que
    // una toma con la luz cayendo de un lado siga encontrándose. Un tablero
    // impreso se fotografía a mano y casi nunca está bien iluminado del todo.
    const bool found = cv::findChessboardCorners(
        gray, cv::Size(spec.innerCols, spec.innerRows), view.corners,
        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE |
            cv::CALIB_CB_FAST_CHECK);
    if (!found || static_cast<int>(view.corners.size()) != spec.cornerCount()) {
        return std::nullopt;
    }

    // Afinado a subpíxel. Sin esto, la calibración se ajusta a esquinas
    // redondeadas al píxel y el error de reproyección no baja de ~0,5 px por
    // mucho que se tomen más vistas: se estaría midiendo la rejilla de píxeles,
    // no la lente.
    cv::cornerSubPix(gray, view.corners, cv::Size(11, 11), cv::Size(-1, -1),
                     cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30,
                                      0.01));

    const cv::Rect bounds = cv::boundingRect(view.corners);
    const double frameArea = static_cast<double>(gray.total());
    view.coverage = frameArea > 0.0 ? bounds.area() / frameArea : 0.0;
    view.center = {static_cast<float>((bounds.x + bounds.width / 2.0) / gray.cols),
                   static_cast<float>((bounds.y + bounds.height / 2.0) / gray.rows)};
    return view;
}

bool BoardCoverage::goodEnough() const {
    // Las cuatro esquinas y siete de las nueve celdas. Las esquinas son
    // innegociables —son las que fijan k1— y el resto da margen para que no haya
    // que clavar una toma en cada celda con el tablero en la mano.
    return cornersTouched == 4 && cellsTouched >= 7;
}

std::string BoardCoverage::advice() const {
    if (goodEnough()) {
        return {};
    }
    if (cornersTouched < 4) {
        return "Falta llevar el tablero a las esquinas del encuadre: van " +
               std::to_string(cornersTouched) +
               " de 4. La curvatura de la lente crece hacia el borde, asi que si el "
               "ajuste no ve nunca una esquina, la adivina — y lo hace sin quejarse.";
    }
    return "Falta repartir mas las tomas: se han tocado " + std::to_string(cellsTouched) +
           " de las 9 zonas del encuadre.";
}

BoardCoverage coverageOf(const std::vector<BoardView>& views) {
    BoardCoverage coverage;
    for (const auto& view : views) {
        const int col = std::clamp(static_cast<int>(view.center.x * 3.0F), 0, 2);
        const int row = std::clamp(static_cast<int>(view.center.y * 3.0F), 0, 2);
        coverage.touched[row * 3 + col] = true;
    }
    for (const bool cell : coverage.touched) {
        if (cell) {
            ++coverage.cellsTouched;
        }
    }
    for (const int corner : {0, 2, 6, 8}) {
        if (coverage.touched[corner]) {
            ++coverage.cornersTouched;
        }
    }
    return coverage;
}

core::Result<LensCalibration> calibrateLens(const std::vector<BoardView>& views,
                                            const BoardSpec& spec) {
    if (!spec.isValid()) {
        return core::Result<LensCalibration>::err(
            "El tablero descrito no vale: se cuentan las esquinas INTERIORES (un tablero "
            "de 10x7 cuadros tiene 9x6) y el lado del cuadro tiene que ser mayor que cero.");
    }
    if (static_cast<int>(views.size()) < kMinimumViews) {
        return core::Result<LensCalibration>::err(
            "Hacen falta al menos " + std::to_string(kMinimumViews) + " tomas del tablero y " +
            "hay " + std::to_string(views.size()) +
            ". Con menos, el ajuste tiene más parámetros que datos fiables: sale un "
            "modelo que reproyecta muy bien sobre sus propias tomas y falla en "
            "cualquier otra.");
    }

    // SE RECHAZA UNA COBERTURA MALA, no se avisa. Medido, y por eso es tajante:
    //
    //   tomas repartidas -> residuo del 0,11 % en el borde
    //   tomas apiñadas   -> residuo del 34,88 %
    //   sin corregir     -> error del 14,01 %
    //
    // O sea que una calibracion mal cubierta no es una calibracion floja: deja
    // las medidas DOS VECES Y MEDIA peor que no corregir nada. Y su error de
    // reproyeccion es 0,404 px, indistinguible del de una buena, porque mide lo
    // bien que el modelo explica las tomas que se le dieron. Devolver eso con un
    // aviso seria poner una trampa detras de una cifra tranquilizadora.
    if (const auto coverage = coverageOf(views); !coverage.goodEnough()) {
        return core::Result<LensCalibration>::err(coverage.advice());
    }

    const cv::Size size = views.front().imageSize;
    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints;
    for (const auto& view : views) {
        if (view.imageSize != size) {
            return core::Result<LensCalibration>::err(
                "Hay tomas de distinta resolución. El modelo de la lente va en píxeles, "
                "así que mezclar resoluciones daría un modelo que no vale para ninguna.");
        }
        if (static_cast<int>(view.corners.size()) != spec.cornerCount()) {
            return core::Result<LensCalibration>::err(
                "Una de las tomas no tiene todas las esquinas del tablero.");
        }
        objectPoints.push_back(boardPoints(spec));
        imagePoints.push_back(view.corners);
    }

    LensCalibration calibration;
    calibration.imageSize = size;
    std::vector<cv::Mat> rotations;
    std::vector<cv::Mat> translations;
    calibration.reprojectionError =
        cv::calibrateCamera(objectPoints, imagePoints, size, calibration.cameraMatrix,
                            calibration.distortion, rotations, translations);
    calibration.views = static_cast<int>(views.size());

    if (!std::isfinite(calibration.reprojectionError) ||
        calibration.reprojectionError > kMaxUsableReprojectionError) {
        return core::Result<LensCalibration>::err(
            "La calibración no cuadra: el error de reproyección es de " +
            std::to_string(calibration.reprojectionError) +
            " px. Casi siempre significa que alguna toma tenía el tablero movido o mal "
            "detectado. Un modelo así deforma las medidas en vez de arreglarlas.");
    }
    return core::Result<LensCalibration>::ok(std::move(calibration));
}

double worstDisplacementPx(const LensCalibration& calibration) {
    if (calibration.cameraMatrix.empty() || calibration.distortion.empty() ||
        calibration.imageSize.empty()) {
        return 0.0;
    }
    // Las cuatro esquinas y los cuatro puntos medios de los lados: el máximo de
    // la distorsión radial está en el borde, y probar solo las esquinas se
    // quedaría corto en encuadres muy alargados.
    const auto w = static_cast<float>(calibration.imageSize.width - 1);
    const auto h = static_cast<float>(calibration.imageSize.height - 1);
    const std::vector<cv::Point2f> probes = {{0.0F, 0.0F}, {w, 0.0F},   {0.0F, h},
                                             {w, h},       {w / 2, 0.0F}, {w / 2, h},
                                             {0.0F, h / 2}, {w, h / 2}};
    std::vector<cv::Point2f> ideal;
    cv::undistortPoints(probes, ideal, calibration.cameraMatrix, calibration.distortion,
                        cv::noArray(), calibration.cameraMatrix);
    double worst = 0.0;
    for (std::size_t i = 0; i < probes.size(); ++i) {
        worst = std::max(worst, cv::norm(cv::Point2f(probes[i] - ideal[i])));
    }
    return worst;
}

bool distortionIsNegligible(const LensCalibration& calibration) {
    return worstDisplacementPx(calibration) < kNegligibleDistortionPx;
}

LensCorrector::LensCorrector(const LensCalibration& calibration) : calibration_(calibration) {
    if (calibration.cameraMatrix.empty() || calibration.distortion.empty() ||
        calibration.imageSize.empty()) {
        return;
    }
    // La MISMA K de salida que de entrada, a propósito.
    //
    // `initUndistortRectifyMap` admite una matriz de salida distinta, y lo
    // habitual en visión 3D es recalcularla para aprovechar todo el sensor. Aquí
    // no: cambiarla cambia la escala en píxeles, y este programa tiene medidas
    // calibradas en mm/px y zonas de trabajo guardadas en píxeles. Enderezar la
    // lente no puede además mover el zoom, o todo lo guardado dejaría de valer.
    cv::initUndistortRectifyMap(calibration.cameraMatrix, calibration.distortion, cv::Mat(),
                                calibration.cameraMatrix, calibration.imageSize, CV_32FC1,
                                mapX_, mapY_);
}

bool LensCorrector::appliesTo(const cv::Size& size) const {
    return isReady() && size == calibration_.imageSize;
}

cv::Mat LensCorrector::apply(const cv::Mat& frame) const {
    if (frame.empty() || !appliesTo(frame.size())) {
        return frame;
    }
    cv::Mat straight;
    // BORDER_CONSTANT y no REPLICATE: al enderezar sobran esquinas sin dato, y
    // repetir el borde inventaría píxeles que la cámara nunca vio, justo en la
    // zona donde alguien podría medir. Un borde negro se ve y no engaña.
    cv::remap(frame, straight, mapX_, mapY_, cv::INTER_LINEAR, cv::BORDER_CONSTANT,
              cv::Scalar(0, 0, 0));
    return straight;
}

// --- Persistencia -----------------------------------------------------------

std::string serializeCalibration(const LensCalibration& calibration) {
    if (calibration.cameraMatrix.empty() || calibration.distortion.empty()) {
        return {};
    }
    std::ostringstream out;
    out.precision(12);
    out << calibration.imageSize.width << ' ' << calibration.imageSize.height << ' '
        << calibration.reprojectionError << ' ' << calibration.views;
    cv::Mat k;
    calibration.cameraMatrix.convertTo(k, CV_64F);
    for (int i = 0; i < 9; ++i) {
        out << ' ' << k.at<double>(i / 3, i % 3);
    }
    cv::Mat d;
    calibration.distortion.convertTo(d, CV_64F);
    out << ' ' << d.total();
    for (std::size_t i = 0; i < d.total(); ++i) {
        out << ' ' << d.at<double>(static_cast<int>(i));
    }
    return out.str();
}

std::optional<LensCalibration> parseCalibration(const std::string& text) {
    std::istringstream in(text);
    LensCalibration calibration;
    int width = 0;
    int height = 0;
    if (!(in >> width >> height >> calibration.reprojectionError >> calibration.views)) {
        return std::nullopt;
    }
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    calibration.imageSize = {width, height};
    calibration.cameraMatrix = cv::Mat::zeros(3, 3, CV_64F);
    for (int i = 0; i < 9; ++i) {
        if (!(in >> calibration.cameraMatrix.at<double>(i / 3, i % 3))) {
            return std::nullopt;
        }
    }
    std::size_t coefficients = 0;
    if (!(in >> coefficients) || coefficients < 4 || coefficients > 14) {
        return std::nullopt;
    }
    calibration.distortion = cv::Mat::zeros(static_cast<int>(coefficients), 1, CV_64F);
    for (std::size_t i = 0; i < coefficients; ++i) {
        if (!(in >> calibration.distortion.at<double>(static_cast<int>(i)))) {
            return std::nullopt;
        }
    }
    return calibration;
}

}  // namespace pci::vision
