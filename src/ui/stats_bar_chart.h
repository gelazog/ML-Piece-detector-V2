#pragma once

#include <QWidget>

#include <vector>

#include "repositories/inspection_repository.h"

namespace pci::ui {

// Gráfico de barras apiladas OK/NG por día, pintado con QPainter (sin librería
// de gráficos externa). Sin Q_OBJECT: solo pinta datos que se le pasan.
class StatsBarChart : public QWidget {
public:
    explicit StatsBarChart(QWidget* parent = nullptr);

    void setData(std::vector<repositories::InspectionRepository::DailyStat> data);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<repositories::InspectionRepository::DailyStat> data_;
};

}  // namespace pci::ui
